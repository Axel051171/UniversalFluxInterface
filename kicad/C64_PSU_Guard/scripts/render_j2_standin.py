"""Copy of the real board with the Phoenix MKDS step model attached to J2 (the MaiXu footprint
ships without a 3D model) - render only, never saved into the project."""
import sys

import pcbnew

board = pcbnew.LoadBoard(sys.argv[1])
j2 = board.FindFootprintByReference("J2")
j2.Models().clear()
m = pcbnew.FP_3DMODEL()
m.m_Filename = ("${KICAD10_3DMODEL_DIR}/TerminalBlock_Phoenix.3dshapes/"
                "TerminalBlock_Phoenix_MKDS-1,5-3-5.08_1x03_P5.08mm_Horizontal.step")
j2.Models().push_back(m)
board.Save(sys.argv[2])
print("saved", sys.argv[2])
