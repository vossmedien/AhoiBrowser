import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
MOBILE_CORE = ROOT / "apps" / "AhoiMobile" / "Sources" / "AhoiMobileCore"


class MobileSyncEventContractTests(unittest.TestCase):
    def test_remote_command_follow_up_has_no_foreground_polling_loop(self) -> None:
        remote_commands = (
            MOBILE_CORE / "CompanionAppModelRemoteCommands.swift"
        ).read_text(encoding="utf-8")
        model = (MOBILE_CORE / "CompanionAppModel.swift").read_text(encoding="utf-8")

        self.assertNotIn("commandFollowUpTasks", remote_commands)
        self.assertNotIn("beginCommandFollowUp", remote_commands)
        self.assertNotIn("Task.sleep", remote_commands)
        self.assertNotIn("commandFollowUpTasks", model)

        # Restart hydration must read the bounded durable command model, not
        # only the labels still present in this process's presentation state.
        self.assertRegex(
            remote_commands,
            r"bridge\.remoteCommandStates\(\s*"
            r"limit: CompanionRemoteCommandRetention\.maximumReadModelCount\s*\)",
        )
        self.assertNotIn("remoteCommandStates(Set(commandLabels.keys))", remote_commands)

    def test_provider_events_remain_the_authoritative_background_trigger(self) -> None:
        model = (MOBILE_CORE / "CompanionAppModel.swift").read_text(encoding="utf-8")
        lifecycle = (
            MOBILE_CORE / "CompanionAppModelSyncLifecycle.swift"
        ).read_text(encoding="utf-8")

        self.assertIn("setEventDrivenSyncHandler", model)
        self.assertIn("scheduleEventDrivenSync", model)
        self.assertIn("bindEventDrivenSync(to: runtime.provider)", lifecycle)
        self.assertIn("providerToCancel?.setEventDrivenSyncHandler(nil)", lifecycle)


if __name__ == "__main__":
    unittest.main()
