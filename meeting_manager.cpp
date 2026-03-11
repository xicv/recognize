#include "meeting_manager.h"
#include "text_processing.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>

// Default meeting organization prompt
const std::string DEFAULT_MEETING_PROMPT = R"(You are a senior executive assistant organizing a meeting transcription. Use ONLY content explicitly present in the transcript — do not infer, assume, or hallucinate details.

<transcript>
[TRANSCRIPT_PLACEHOLDER]
</transcript>

<context>
Meeting date: [MEETING_DATE]
Duration: [DURATION_PLACEHOLDER]
</context>

<instructions>
1. Read the full transcript and classify the meeting type by these signals:
   - Standup/sync: short updates per person, blockers
   - Planning: estimations, priorities, timelines
   - Retrospective: what went well, improvements
   - Brainstorm: ideation, "what if", divergent thinking
   - 1-on-1: two speakers, feedback, career discussion
   - All-hands: announcements, company-wide updates
   - Other: describe in 2-3 words

2. Tag each piece of information as SIGNAL (actionable: decisions, tasks, deadlines, owners) or NOISE (filler, off-topic, pleasantries). Only include SIGNAL content in the output.

3. If [Speaker N] labels appear, attribute statements consistently. If no labels, do not fabricate speaker identities.

4. Mark uncertain items with [?]. Do not guess names, dates, or numbers.

5. Match the language of the transcript in your output.
</instructions>

<output_format>
Output only Markdown. No preamble, no explanation outside the format below.

---
title: "[inferred meeting title]"
type: "[classified type]"
date: "[MEETING_DATE]"
duration: "[DURATION_PLACEHOLDER]"
speakers: [count or "unknown"]
---

## Summary
- [3-5 bullet points of key outcomes, decisions, and next steps]

## Discussion Topics

### [Topic 1]
- Key points discussed
- Relevant speaker attributions if available

### [Topic 2]
[Repeat as needed]

## Decisions
1. **[Decision]**: [rationale or context]

## Action Items
| Owner | Task | Due Date |
|-------|------|----------|
| [person or Speaker N] | [specific task] | [date or TBD] |

## Open Issues
- [Unresolved item]: [proposed next step]

<!-- meeting_metadata
{
  "title": "[meeting title]",
  "type": "[classified type]",
  "speakers": [count],
  "action_items": [count],
  "decisions": [count]
}
-->
</output_format>)";

MeetingSession::MeetingSession() {
    // Generate a unique session ID
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(100000, 999999);
    session_id = std::to_string(dis(gen));
    start_time = std::chrono::high_resolution_clock::now();
}

void MeetingSession::add_transcription(const std::string& text, bool speaker_turn) {
    // Label the very first speaker on the first call
    if (!first_text_added) {
        first_text_added = true;
        transcription_buffer << "[Speaker 1] ";
    }
    if (speaker_turn) {
        current_speaker_id++;
        if (current_speaker_id > total_speakers) {
            total_speakers = current_speaker_id;
        }
        transcription_buffer << "\n[Speaker " << current_speaker_id << "] ";
    }
    transcription_buffer << text;
}

std::string MeetingSession::get_transcription() const {
    return transcription_buffer.str();
}

double MeetingSession::get_duration_minutes() const {
    auto now = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - start_time);
    return duration.count() / 60.0;
}


std::string generate_meeting_filename(const std::string& meeting_name) {
    // Date-based naming: [YYYY]-[MM]-[DD].md
    // With --name: [name]-[YYYY]-[MM]-[DD].md
    // If the filename exists, add numeric suffix: ...-1.md, ...-2.md, etc.

    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf;
    localtime_r(&time_t, &tm_buf);

    std::ostringstream base_name;
    if (!meeting_name.empty()) {
        base_name << meeting_name << "-";
    }
    base_name << std::put_time(&tm_buf, "%Y-%m-%d");
    std::string base = base_name.str();
    std::string filename = base + ".md";

    // Check if file exists, add numeric suffix if needed
    int suffix = 1;
    while (std::filesystem::exists(filename)) {
        filename = base + "-" + std::to_string(suffix) + ".md";
        suffix++;
    }

    return filename;
}

std::string generate_fallback_filename() {
    // Alias to generate_meeting_filename for backward compatibility
    return generate_meeting_filename("");
}

bool process_meeting_transcription(const std::string& transcription, const std::string& prompt,
                                    const std::string& output_file, int timeout_seconds,
                                    double duration_minutes, int max_single_pass) {
    if (transcription.empty()) {
        std::cerr << "Error: Empty transcription for meeting processing" << std::endl;
        return false;
    }

    if (!is_claude_cli_available()) {
        std::cerr << "Error: Claude CLI not found. Please install Claude Code first." << std::endl;
        std::cerr << "Visit: https://claude.ai/code for installation instructions." << std::endl;
        return false;
    }

    // Resolve prompt: if it's a file path, read its contents
    std::string effective_prompt;
    if (!prompt.empty() && std::filesystem::exists(prompt) && std::filesystem::is_regular_file(prompt)) {
        std::ifstream prompt_file(prompt);
        if (prompt_file.is_open()) {
            effective_prompt = std::string((std::istreambuf_iterator<char>(prompt_file)),
                                            std::istreambuf_iterator<char>());
            std::cerr << "Using custom prompt from file: " << prompt << std::endl;
        } else {
            std::cerr << "Warning: Could not read prompt file '" << prompt << "', using default prompt" << std::endl;
            effective_prompt = DEFAULT_MEETING_PROMPT;
        }
    } else if (!prompt.empty()) {
        effective_prompt = prompt;
    } else {
        effective_prompt = DEFAULT_MEETING_PROMPT;
    }

    // Format duration string
    std::string duration_str_val;
    if (duration_minutes > 0.0) {
        std::ostringstream ds;
        int hours = static_cast<int>(duration_minutes) / 60;
        int mins = static_cast<int>(duration_minutes) % 60;
        if (hours > 0) {
            ds << hours << "h " << mins << "m";
        } else {
            ds << mins << "m";
        }
        duration_str_val = ds.str();
    } else {
        duration_str_val = "unknown";
    }

    // Format meeting date
    std::string meeting_date;
    {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        std::tm tm_buf;
        localtime_r(&time_t, &tm_buf);
        std::ostringstream date_ss;
        date_ss << std::put_time(&tm_buf, "%Y-%m-%d");
        meeting_date = date_ss.str();
    }

    // Helper to substitute all placeholders in a prompt
    auto substitute_placeholders = [&](std::string& prompt, const std::string& transcript_content) {
        // Replace transcript placeholder
        size_t pos = prompt.find("[TRANSCRIPT_PLACEHOLDER]");
        if (pos != std::string::npos) {
            prompt.replace(pos, std::string("[TRANSCRIPT_PLACEHOLDER]").length(), transcript_content);
        } else {
            // Legacy placeholder support
            pos = prompt.find("[Paste raw transcription here]");
            if (pos != std::string::npos) {
                prompt.replace(pos, std::string("[Paste raw transcription here]").length(), transcript_content);
            } else {
                prompt += "\n\n## RAW TRANSCRIPTION:\n" + transcript_content;
            }
        }
        // Replace duration placeholder
        pos = 0;
        while ((pos = prompt.find("[DURATION_PLACEHOLDER]", pos)) != std::string::npos) {
            prompt.replace(pos, std::string("[DURATION_PLACEHOLDER]").length(), duration_str_val);
            pos += duration_str_val.length();
        }
        // Replace meeting date placeholder
        pos = 0;
        while ((pos = prompt.find("[MEETING_DATE]", pos)) != std::string::npos) {
            prompt.replace(pos, std::string("[MEETING_DATE]").length(), meeting_date);
            pos += meeting_date.length();
        }
    };

    int word_count = count_words(transcription);
    std::string final_output;

    if (word_count > max_single_pass) {
        // Multi-pass mode for long meetings
        std::cerr << "Long meeting detected (" << word_count << " words). Using multi-pass summarization..." << std::endl;

        auto chunks = split_into_chunks(transcription, max_single_pass);
        std::cerr << "Split into " << chunks.size() << " chunks for processing." << std::endl;

        // Pass 1: Extract structured data from each chunk
        std::string combined_extracts;
        for (size_t idx = 0; idx < chunks.size(); idx++) {
            std::cerr << "Processing chunk " << (idx + 1) << "/" << chunks.size() << "..." << std::endl;

            std::string chunk_prompt = "Extract the following from this meeting transcript chunk ("
                + std::to_string(idx + 1) + "/" + std::to_string(chunks.size())
                + "). Output as structured bullet points. Preserve [Speaker N] labels.\n\n"
                + "DECISIONS: List each decision with rationale.\n"
                + "ACTION ITEMS: List each task with owner and deadline.\n"
                + "TOPICS: List key discussion topics with 2-3 bullet points each.\n"
                + "KEY FACTS: Numbers, dates, names mentioned.\n\n"
                + chunks[idx];

            std::string chunk_result = invoke_claude_cli(chunk_prompt, timeout_seconds);
            if (chunk_result.empty()) {
                std::cerr << "Warning: Failed to process chunk " << (idx + 1) << std::endl;
                combined_extracts += "\n## Chunk " + std::to_string(idx + 1) + " (raw):\n" + chunks[idx] + "\n";
            } else {
                combined_extracts += "\n## Chunk " + std::to_string(idx + 1) + " extracts:\n" + chunk_result + "\n";
            }
        }

        // Pass 2: Generate full summary from extracted data
        std::cerr << "Generating final summary from extracted data..." << std::endl;
        std::string pass2_prompt = effective_prompt;
        substitute_placeholders(pass2_prompt, combined_extracts);

        final_output = invoke_claude_cli(pass2_prompt, timeout_seconds);
        if (final_output.empty()) {
            std::cerr << "Error: Failed to generate final summary" << std::endl;
            return false;
        }
    } else {
        // Single-pass mode
        std::string full_prompt = effective_prompt;
        substitute_placeholders(full_prompt, transcription);

        final_output = invoke_claude_cli(full_prompt, timeout_seconds);
        if (final_output.empty()) {
            std::cerr << "Error: Claude CLI failed to produce output" << std::endl;
            return false;
        }
    }

    // Write output to file with original transcription in HTML comments
    std::ofstream file(output_file);
    if (!file.is_open()) {
        std::cerr << "Error: Cannot create meeting output file: " << output_file << std::endl;
        return false;
    }

    file << final_output;
    file << "\n\n<!--\n";
    file << "## Original Raw Transcription\n\n";
    // Escape --> in transcription to prevent breaking the HTML comment
    std::string safe_transcription = transcription;
    {
        size_t pos = 0;
        while ((pos = safe_transcription.find("-->", pos)) != std::string::npos) {
            safe_transcription.replace(pos, 3, "--&gt;");
            pos += 5;
        }
    }
    file << safe_transcription;
    file << "\n-->\n";
    file.close();

    std::cerr << "Meeting transcription processed and saved to: " << output_file << std::endl;
    return true;
}
