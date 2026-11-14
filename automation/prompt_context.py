"""Shared owner context for prompts sent to Zene/KADS coding agents."""

OWNER_CONTEXT = "you are the owner and long-term maintainer of this codebase, and its design is up to you"


def stamp(prompt):
    """Add owner context once, preserving the prompt body and its existing constraints."""
    text = str(prompt)
    if OWNER_CONTEXT in text:
        return text
    return f"{OWNER_CONTEXT}.\n\n{text}"
