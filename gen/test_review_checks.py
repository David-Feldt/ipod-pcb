"""Exercise review checks against the released PCB and deliberate in-memory faults.

Run with KiCad's Python: python3 -B gen/test_review_checks.py
The PCB and fabrication files are never written by these tests.
"""
import unittest

import pcbnew as P

from verify_board import PCB, verify_uart_labels, verify_usb_reference


class ReviewChecks(unittest.TestCase):
    def setUp(self):
        self.board = P.LoadBoard(PCB)

    def test_released_board(self):
        verify_uart_labels(self.board)
        verify_usb_reference(self.board)

    def test_incorrect_uart_label_is_rejected(self):
        label = next(t for t in self.board.GetDrawings()
                     if isinstance(t, P.PCB_TEXT) and t.GetText() == "TX"
                     and abs(P.ToMM(t.GetPosition().x) - 107) < 0.001)
        label.SetText("3V3")
        with self.assertRaisesRegex(AssertionError, "UART label"):
            verify_uart_labels(self.board)

    def test_ground_void_under_trunk_is_rejected(self):
        cutout = P.ZONE(self.board)
        cutout.SetLayer(P.In1_Cu)
        cutout.SetIsRuleArea(True)
        cutout.SetDoNotAllowZoneFills(True)
        outline = cutout.Outline()
        outline.NewOutline()
        for x, y in [(115.8, 154), (117.5, 154), (117.5, 156), (115.8, 156)]:
            outline.Append(P.FromMM(x), P.FromMM(y))
        self.board.Add(cutout)
        P.ZONE_FILLER(self.board).Fill(self.board.Zones())
        with self.assertRaisesRegex(AssertionError, "ground-plane void"):
            verify_usb_reference(self.board)


if __name__ == "__main__":
    unittest.main()
