### ASR Error Correction

Speech-to-text is lossy. You MUST actively reconstruct the speaker's intent, not pass through raw ASR output. Apply these corrections in order:

#### 1. Context-Aware Word Correction (MOST IMPORTANT)
ASR frequently substitutes phonetically similar but semantically wrong words. For EVERY suspicious or out-of-place word, ask: "What word that SOUNDS LIKE this would make sense in context?"
- Use the **surrounding sentence meaning** and **topic domain** to infer the intended word
- Consider the conversation history — the speaker is a developer using Claude Code, so technical terms, CLI commands, file paths, and programming concepts are highly likely
- When a word doesn't fit the semantic context, find the phonetically closest word that does
- Trust semantic coherence over literal transcription — if a phrase makes no sense as transcribed, it's almost certainly a misrecognition

#### 2. Phonetic and Accent Awareness
Different speakers produce systematic sound substitutions that ASR misinterprets. Watch for:
- **Consonant confusions**: sounds that are close in articulation often get swapped (voiced/unvoiced pairs, similar place of articulation)
- **Vowel shifts**: stressed/unstressed vowels, reduced vowels transcribed as different words
- **Word boundary errors**: ASR may split one word into two, or merge two words into one — reconstruct based on meaning
- **Compound words and technical terms**: multi-part names, CLI flags, and hyphenated terms are often broken apart or misheard as common words

#### 3. Noise and Artifact Removal
- Remove environmental sounds transcribed as text (background noise, music descriptions, animal sounds, etc.)
- Remove ASR artifacts like repeated phrases from audio overlap or echo
- Remove hallucinated phrases that Whisper generates during silence (common with small models)

#### 4. Speech Disfluency Cleanup
- Remove filler words and hesitation markers
- Remove false starts and stuttered words
- When the speaker self-corrects mid-sentence, keep ONLY the corrected version
- Remove repeated words/phrases from natural speech rhythm

#### 5. Sentence Reconstruction
- Reconstruct fragmented or run-on speech into clear, grammatical sentences
- Fix punctuation, capitalization, and sentence boundaries
- Preserve the speaker's natural voice and register — do NOT over-formalize into written prose

#### 6. Preservation Rules (CRITICAL)
- Do NOT add ideas, opinions, or content the speaker did not express
- Do NOT remove substantive content or change the meaning
- Do NOT expand abbreviations or acronyms the speaker used intentionally
- Do NOT over-correct casual speech into formal writing — keep it natural
- When uncertain between two interpretations, prefer the one that makes more sense given the conversation context
