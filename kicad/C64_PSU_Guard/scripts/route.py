"""Export DSN / import SES / fill zones for a board.  usage: route.py export|import <board> <dsn|ses>"""
import sys

import pcbnew

mode, path, io = sys.argv[1:4]
board = pcbnew.LoadBoard(path)
if mode == "export":
    ok = pcbnew.ExportSpecctraDSN(board, io)
    print("export", ok)
else:
    ok = pcbnew.ImportSpecctraSES(board, io)
    print("import", ok)
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    board.Save(path)
    print("tracks", len(board.GetTracks()))
