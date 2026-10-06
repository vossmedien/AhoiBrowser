# 47885f36 SSD interruption and incremental continuation

Frozen source47885f36, Chromium155.0.8059.26, Inhouse only.
The first cold run stopped16:40:20Z after about29,963/60,625 steps when the
approved SSD containing source/tools disappeared. `ssd-abort-excerpt.txt` binds
the original raw target log by SHA256/size and preserves the concrete failures.

`ssd-recovery-inputs.json` records exact restored workaround-original hashes
and mtime preservation, plus the three owned orphan Clangs stopped after
ancestry/output checks. The raw log and original temporary backups remain on
the target; neither outputs nor source were reset. Expected APFS UUID and both
engine pins were checked before recovery.

`phases.log` is a snapshot showing terminal1 of the original run and the single
bounded resume on the same source/output, runner47404. It does not prove final
build, signing, installation or E2E success. The current checkpoint owns those
later gates. No development-Mac app was replaced.
