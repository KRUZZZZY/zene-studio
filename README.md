# Zene Studio train records

Append-only snapshots of promoted-train identities, first-parent changes, CI links, local evidence, repair records and the automation that maintains them. A snapshot is evidence of what ran, not a green-CI or release certificate.

Start with versions.json and trains/<internal-train>/version.json. ci.json links the product source branch and hosted CI. Evidence logs are gzip-compressed; evidence-index.json records SHA-256 hashes of the original bytes. Missing evidence stays missing. Repair patches preserve unmerged deltas and original commit IDs; inspect repairs.json for base/source IDs and verification. Restoring a queue requires review against the current base.

Git history preserves earlier snapshots when CI or repair status changes. Product source history is separately backed by ci/m0-train-N-<SHA> branches; this records branch deliberately has no GitHub workflows. Automation files are recovery copies; restore selected files without replacing live queues blindly.

Every update is scanned for secrets before compression and again before push. No credentials, provider configuration or agent conversations are collected.
