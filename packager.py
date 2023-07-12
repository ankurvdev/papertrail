import os
import shutil
import subprocess

if True:
    import externaltools
    import buildenv


class Packager:
    def __init__(self, subdir: str):
        raise Exception(subdir)

    def Build(self):
        pass

    @ staticmethod
    def CreateArgParser(parser):
        parser.add_argument("subdir", type=str, default=None, help="")
        #parser.add_argument("--subdir", type=str, default=None, help="")
        #parser.add_argument("--output", type=str, default=None, help="")
        #parser.add_argument("--embedexe", type=str, default=None, help="")
        return parser

    @ staticmethod
    def ArgHandler(args):
        raise Exception(args)
        Packager(args.subdir).Build()
