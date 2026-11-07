"""Tests für den Ablaufplan (steps.py). Ausführen mit:

    python3 -m unittest installer/test_steps.py -v

Die Tests brauchen kein Telefon, kein Fenster und keine Netzwerkverbindung.
"""

import os
import tempfile
import unittest

import steps


def ids(plan):
    return [s.id for s in plan]


class BuildPlanTests(unittest.TestCase):

    def setUp(self):
        # Ein echtes Halium-Abbild gibt es nicht im Test, also eine leere Datei
        fd, self.halium = tempfile.mkstemp()
        os.close(fd)

    def tearDown(self):
        os.unlink(self.halium)

    def test_build_only_writes_nothing_to_phone(self):
        plan = steps.build_plan(steps.MODE_BUILD, "phone")
        self.assertEqual(ids(plan), ["fetch", "kernel", "boot", "rootfs"])
        self.assertFalse(steps.has_destructive(plan))
        self.assertFalse(steps.needs_fastboot(plan))

    def test_build_rootfs_uses_selected_profile(self):
        plan = steps.build_plan(steps.MODE_BUILD, "desktop")
        rootfs = plan[-1]
        self.assertIn("PROFILE=desktop", rootfs.argv)
        self.assertTrue(rootfs.root)

    def test_full_phone_order(self):
        plan = steps.build_plan(steps.MODE_FULL, "phone", self.halium)
        self.assertEqual(ids(plan), [
            "fetch", "kernel", "boot", "rootfs",
            "userdata", "boot-b", "wait", "host-nat", "stage2",
        ])

    def test_full_with_stock_flash_comes_first_of_the_flashing(self):
        plan = steps.build_plan(steps.MODE_FULL, "phone", self.halium, flash_stock=True)
        i = ids(plan).index("flash-stock")
        self.assertEqual(ids(plan)[i - 1], "rootfs")
        self.assertLess(i, ids(plan).index("userdata"))
        self.assertTrue(plan[i].destructive)

    def test_full_destructive_steps_are_marked(self):
        plan = steps.build_plan(steps.MODE_FULL, "phone", self.halium)
        destructive = [s.id for s in plan if s.destructive]
        self.assertEqual(destructive, ["userdata", "boot-b"])
        self.assertTrue(steps.has_destructive(plan))

    def test_full_server_stops_after_flashing(self):
        plan = steps.build_plan(steps.MODE_FULL, "server")
        self.assertEqual(ids(plan)[-1], "boot-b")
        self.assertNotIn("stage2", ids(plan))
        self.assertNotIn("wait", ids(plan))

    def test_full_desktop_stage2_is_optional(self):
        without = steps.build_plan(steps.MODE_FULL, "desktop")
        self.assertNotIn("stage2", ids(without))
        self.assertNotIn("desktop", ids(without))  # Das Abbild ist schon Desktop
        with_gpu = steps.build_plan(steps.MODE_FULL, "desktop", self.halium)
        self.assertIn("stage2", ids(with_gpu))

    def test_stage2_only_on_running_phone(self):
        plan = steps.build_plan(steps.MODE_STAGE2, "phone", self.halium)
        self.assertEqual(ids(plan), ["wait", "host-nat", "stage2"])
        self.assertFalse(steps.needs_fastboot(plan))
        self.assertEqual(plan[-1].argv, ["scripts/setup-phone.sh", self.halium])

    def test_server_and_desktop_on_running_phone(self):
        self.assertEqual(ids(steps.build_plan(steps.MODE_SERVER)),
                         ["wait", "host-nat", "server"])
        self.assertEqual(ids(steps.build_plan(steps.MODE_DESKTOP)),
                         ["wait", "host-nat", "desktop"])

    def test_host_nat_is_optional_and_root(self):
        plan = steps.build_plan(steps.MODE_STAGE2, "phone", self.halium)
        nat = [s for s in plan if s.id == "host-nat"][0]
        self.assertTrue(nat.optional)
        self.assertTrue(nat.root)

    def test_phone_profile_needs_halium_image_for_full(self):
        with self.assertRaises(steps.PlanError):
            steps.build_plan(steps.MODE_FULL, "phone", "")

    def test_phone_profile_needs_halium_image_for_stage2(self):
        with self.assertRaises(steps.PlanError):
            steps.build_plan(steps.MODE_STAGE2, "phone", "")

    def test_missing_halium_file_is_an_error(self):
        with self.assertRaises(steps.PlanError):
            steps.build_plan(steps.MODE_STAGE2, "phone", "/nicht/vorhanden.img")

    def test_unknown_mode_and_profile(self):
        with self.assertRaises(steps.PlanError):
            steps.build_plan("kaputt")
        with self.assertRaises(steps.PlanError):
            steps.build_plan(steps.MODE_BUILD, "tablet")

    def test_needs_sudo_only_when_a_root_step_exists(self):
        self.assertTrue(steps.needs_sudo(steps.build_plan(steps.MODE_BUILD)))
        self.assertFalse(steps.needs_sudo(steps.build_plan(steps.MODE_SERVER)[:1]))


class DescribeTests(unittest.TestCase):

    def test_describe_marks_destructive_and_sudo(self):
        plan = steps.build_plan(steps.MODE_FULL, "server")
        text = "\n".join(steps.describe(plan))
        self.assertIn("LÖSCHT DATEN", text)
        self.assertIn("sudo", text)
        self.assertIn("fastboot flash userdata out/rootfs.simg", text)

    def test_describe_wait_step(self):
        text = "\n".join(steps.describe(steps.build_plan(steps.MODE_SERVER)))
        self.assertIn("warten auf Telefon", text)


class PrerequisiteTests(unittest.TestCase):

    def test_prerequisite_names_for_build(self):
        names = [c.name for c in steps.prerequisites(steps.build_plan(steps.MODE_BUILD))]
        self.assertIn("Programm: mkbootimg", names)
        self.assertIn("Arch-Linux-ARM-Basissystem", names)

    def test_server_mode_does_not_need_build_tools(self):
        names = [c.name for c in steps.prerequisites(steps.build_plan(steps.MODE_SERVER))]
        self.assertNotIn("Programm: mkbootimg", names)
        self.assertIn("Programm: ssh", names)

    def test_halium_check(self):
        self.assertFalse(steps.halium_check("").ok)
        self.assertFalse(steps.halium_check("/nicht/vorhanden.img").ok)
        with tempfile.NamedTemporaryFile() as f:
            self.assertTrue(steps.halium_check(f.name).ok)


if __name__ == "__main__":
    unittest.main()
