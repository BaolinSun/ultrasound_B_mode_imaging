from pathlib import Path
import sys
import tempfile
import unittest
import re

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'build/scripts'))
import cpu1_fsbl


class Cpu1Integration(unittest.TestCase):
    def test_local_fsbl_hook_preserves_user_code_and_is_repeatable(self):
        with tempfile.TemporaryDirectory() as tmp:
            d = Path(tmp) / 'ubmi_p/zynq_fsbl'
            d.mkdir(parents=True)
            p = d / 'fsbl_hooks.c'
            original = '#include "fsbl_hooks.h"\nu32 FsblHookBeforeHandoff(void)\n{\n u32 Status;\n Status = XST_SUCCESS;\n user_hook();\n return (Status);\n}\n'
            p.write_text(original)
            cpu1_fsbl.configure_cpu1_fsbl(tmp)
            once = p.read_bytes()
            self.assertIn(b'user_hook();', once)
            self.assertIn(b'return (Status);', once)
            cpu1_fsbl.configure_cpu1_fsbl(tmp)
            self.assertEqual(once, p.read_bytes())
            for name, origin in cpu1_fsbl.FILES.items():
                self.assertEqual((d / name).read_bytes(), origin.read_bytes())
            (d / 'cpu1_control.h').write_text('stale')
            with self.assertRaisesRegex(RuntimeError, 'Stale'):
                cpu1_fsbl.verify_cpu1_fsbl(tmp)
            cpu1_fsbl.configure_cpu1_fsbl(tmp)
            for name in ('cpu1_control.c', 'cpu1_platform.h'):
                (d/name).write_bytes((ROOT/'common'/name).read_bytes())
            cpu1_fsbl.configure_cpu1_fsbl(tmp)
            self.assertFalse((d/'cpu1_control.c').exists())
            self.assertFalse((d/'cpu1_platform.h').exists())
            edited=d/'cpu1_platform.h'
            edited.write_text('user edits')
            with self.assertRaisesRegex(RuntimeError,'user edits'):
                cpu1_fsbl.configure_cpu1_fsbl(tmp)
            self.assertEqual(edited.read_text(),'user edits')
            edited.unlink()
            p.write_text(original.replace('Status = XST_SUCCESS;', 'different_template();'))
            with self.assertRaisesRegex(RuntimeError, 'hook changed'):
                cpu1_fsbl.configure_cpu1_fsbl(tmp)

    def test_application_order_source_sync_and_irq_cpu_numbers(self):
        roots = [ROOT / 'build/src/ps', ROOT / 'workspace/B_mode_focus_imaging/src']
        for relative in ('system_ctrl/cpu1_power.c', 'system_ctrl/cpu1_power.h', 'uart/uart_log.c', 'uart/uart.c', 'usb/xusbps_COMMS.c'):
            self.assertEqual((roots[0]/relative).read_text(), (roots[1]/relative).read_text(), relative)
        for root in roots:
            main = (root / 'main.c').read_text(errors='replace').split('int main()')[1]
            self.assertLess(main.index('zynq_uart_log_init()'), main.index('cpu1_power_init()'))
            self.assertLess(main.index('cpu1_power_init()'), main.index('intr_init()'))
            self.assertNotIn('cpu1 start', main)
            for relative in ('uart/uart_log.c', 'uart/uart.c', 'usb/xusbps_COMMS.c'):
                text = (root / relative).read_text()
                calls = re.findall(r'XScuGic_InterruptMaptoCpu\([^,]+,\s*([^,]+),', text)
                self.assertTrue(calls)
                self.assertTrue(all(arg.strip().startswith('0U') for arg in calls), calls)
            uart = (root/'uart/uart.c').read_text()
            self.assertIn('cpu1_disabled=%u', uart)
            self.assertNotIn('cpu1_power_is_disabled() &&', uart)
        generator = (ROOT/'build/scripts/build_vitis_2026_1.py').read_text()
        self.assertLess(generator.index('build_cpu1_fsbl(WORKSPACE'), generator.index('if STAGE == "platform"'))
        adapter = (roots[0]/'system_ctrl/cpu1_power.c').read_text()
        self.assertIn('WARNING', adapter)
        self.assertNotIn('while', adapter)
        self.assertNotIn('imaging_power', adapter)
        init=adapter.split('void cpu1_power_init(void)')[1].split('const char *cpu1_power_disable')[0]
        self.assertNotIn('cpu1_platform_disable(',init)
        self.assertNotIn('Xil_Out32',init)
        fsbl=(ROOT/'build/src/fsbl/cpu1_fsbl.h').read_text()
        self.assertNotIn('cpu1_platform_disable',fsbl)
        self.assertNotIn('Xil_Out32',fsbl)
        self.assertIn('startup=observe',fsbl)


if __name__ == '__main__':
    unittest.main()
