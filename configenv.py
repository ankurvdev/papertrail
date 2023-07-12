#!/usr/bin/python3
import configparser
import os
import pathlib
import sys
import threading
from time import sleep
from typing import Optional


class ConfigException(Exception):
    pass


class ConfigMonitor:
    def __init__(self):
        self.stopRequested = False
        self.thrd = threading.Thread(target=lambda obj: obj.ThreadLoop(), args=[self])
        self.thrd.start()

    def ThreadLoop(self):
        while not self.stopRequested:
            while not ConfigEnv(None).ValidateConfig():
                sleep(0.5)
            sleep(1)

    def Stop(self):
        self.stopRequested = True
        self.thrd.join()

    def __del__(self):
        self.Stop()


class ConfigEnv:
    Unspecified = "<UNSPECIFIED>"
    InputRequired = "<INPUTREQUIRED>"
    Monitor: Optional[ConfigMonitor] = None

    @staticmethod
    def StartMonitor():
        ConfigEnv.Monitor = ConfigMonitor()

    @staticmethod
    def StopMonitor():
        if ConfigEnv.Monitor:
            ConfigEnv.Monitor.Stop()
        ConfigEnv.Monitor = None

    def __init__(self, reporoot: Optional[str]):
        self._globalConfigFile = pathlib.Path(__file__).parent / ".config"
        self._repoConfigFile: Optional[pathlib.Path] = pathlib.Path(reporoot) / ".config" if reporoot else None

    def _IsValidValue(self, value: str):
        return value is not None and value != self.Unspecified and value != self.InputRequired

    def _ReadConfigValues(self, section):
        configvalues = {}
        configvalues['PYTHON_PATH'] = pathlib.Path(sys.executable).parent.as_posix()
        cfg = configparser.ConfigParser()
        if self._globalConfigFile.is_file():
            cfg.read(self._globalConfigFile)
        if self._repoConfigFile and self._repoConfigFile.is_file():
            cfg.read(self._repoConfigFile)
        configvalues.update(dict(cfg[section]))
        return configvalues

    def _WriteConfig(self, section, name, value):
        cfg = configparser.ConfigParser()
        outfile = self._repoConfigFile if self._repoConfigFile is not None and self._repoConfigFile.exists() else self._globalConfigFile
        if outfile.is_file():
            cfg.read(outfile)
        cfg[section][name] = value
        with outfile.open("w") as fd:
            cfg.write(fd)

    def ValidateConfig(self):
        cfg = configparser.ConfigParser()
        outfile = self._repoConfigFile if self._repoConfigFile is not None and self._repoConfigFile.exists() else self._globalConfigFile
        if outfile.is_file():
            cfg.read(outfile)
        for section in cfg:
            for k, v in cfg[section].items():
                if not self._IsValidValue(v):
                    v = input("Enter Value for " + k + ":")
                    self._WriteConfig(section, k, v)
                    return False
        return True

    def _ReadConfig(self, name: str, section, default: Optional[str]) -> str:
        for envname in [name, name.lower(), name.upper()]:
            if envname in os.environ:
                return os.environ.get(envname)

        for i in range(30 * 60):  # Wait max 30 mins
            configvalues = self._ReadConfigValues(section)
            rslt: str | None = configvalues.get(name.lower(), None)
            if self._IsValidValue(rslt):
                return rslt.strip()
            if i == 0:
                if default is not None:
                    return default
                sys.stderr.write(f"Invalid value {name}::{rslt} :: {configvalues}\n")
                self._WriteConfig(section, name, self.InputRequired)
            sleep(1)
        raise ConfigException(f"Cannot find value for {name}")

    def GetConfigPath(self, name: str, section: str = "DEFAULT", default: Optional[str] = None, make: bool = False) -> pathlib.Path:
        p: pathlib.Path = pathlib.Path(os.path.expandvars(os.path.expanduser(self._ReadConfig(name, section, default))))
        if make:
            p.mkdir(exist_ok=True)
        if not p.exists():
            raise ConfigException(f"Cannot find path {p.as_posix()}")
        return p

    def GetConfigStr(self, name: str, section: str = "DEFAULT", default: Optional[str] = None) -> str:
        return self._ReadConfig(name, section, default)
