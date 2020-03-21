#!/usr/bin/python3
import os
import os.path
import sys
import argparse
import urllib.request
import zipfile
import subprocess
import hashlib
import inspect
import shutil
import time
import datetime
from html.parser import HTMLParser
import ssl
import platform
import json
import glob
import pathlib
import threading

UWPMODE = False
URL_CMAKEZIP = "https://github.com/Kitware/CMake/releases/download/v3.16.0/cmake-3.16.0-win64-x64.zip"
URL_NOTEPAD = "http://download.notepad-plus-plus.org/repository/7.x/7.8.1/npp.7.8.1.bin.x64.zip"
URL_IMAGEMAGICK = "https://imagemagick.org/download/binaries/ImageMagick-7.0.9-5-portable-Q16-x64.zip"
URL_VSWHERE = "https://github.com/Microsoft/vswhere/releases/download/2.6.7/vswhere.exe"
UWP_SDK_VERSION = "10.0.18362.0"
UWP_GENERATOR = "Visual Studio 16 2019"
EXE_GENERATOR = "Visual Studio 16 2019"
MAKEAPPX_BINARY = "C:/Program Files (x86)/Windows Kits/10/bin/10.0.18362.0/x86/makeappx.exe"
SIGNTOOL_BINARY = "C:/Program Files (x86)/Windows Kits/10/bin/10.0.18362.0/x86/signtool.exe"

root = os.path.abspath(os.path.dirname(__file__))
CONFIGFILE = os.path.join(root, ".config")
def ReadConfig(name):
    if not os.path.exists(CONFIGFILE): return None
    with open(CONFIGFILE) as f:
        for line in f:
            key,value = line.split("=")
            if key == name: return value.strip()
    return None

def WriteConfig(name, value):
    with open(CONFIGFILE, "a") as f:
        f.write(name + "=" + value + "\n")

def DetectConfig(name):
    if name in os.environ: return os.environ[name]
    value = ReadConfig(name)
    while value == None or len(value) == 0: 
        value = input(name + ":")
        WriteConfig(name, value)
    return value

def GenerateCMakeSettings(d):

    template = """
    {
      "name": "__Name__",
      "generator": "__Generator__",
      "configurationType": "__Config__",
      "inheritEnvironments": [
        "msvc_x64___Architecture__"
      ],
      "buildRoot": "DEVEL_BUILDPATH",
      "installRoot": "DEVEL_BUILDPATH",
      "cmakeCommandArgs": "__Args__",
      "buildCommandArgs": "",
      "ctestCommandArgs": ""
    }
    """
    configurations = []

    for name,config in GeneratorConfig.items():
        buildpath = GetBuildDir(d, name).replace("\\", "/")
        templatestr = template.replace("DEVEL_BUILDPATH", buildpath)
        templatestr = templatestr.replace("__Name__", name)

        for var,val in GeneratorConfig[name].items():
            templatestr = templatestr .replace("__" + var + "__", val)
        configurations.append(json.loads(templatestr))
    alltemplates = {"configurations": configurations}
    contents = ""
    if os.path.exists(os.path.join(d, "CMakeSettings.json")):
        with open(os.path.join(d, "CMakeSettings.json")) as f:
             contents = f.read()
    newcontents = json.dumps(alltemplates, indent=4)
    if contents != newcontents:
        print(contents)
        with open(os.path.join(d, "CMakeSettings.json"), "w") as f:
            f.write(newcontents)
        print(newcontents)

BINPATH = os.path.expanduser(DetectConfig("DEVEL_BINPATH"))
BUILDDIR = os.path.expanduser(DetectConfig("DEVEL_BUILDPATH"))
TMPDIR = os.path.expanduser(DetectConfig("TMP"))

os.makedirs(os.path.expanduser(BINPATH), exist_ok=True)
os.makedirs(os.path.expanduser(BUILDDIR), exist_ok=True)
os.makedirs(os.path.expanduser(TMPDIR), exist_ok=True)

class HTMLUrlExtractor(HTMLParser):
    def __init__(self, url):
        text = urllib.request.urlopen(url, timeout=10, context=ssl._create_unverified_context()).read().decode("utf-8")
        self.baseurl = url
        self.urls = {}
        self.href = None
        self.text = None
        super(HTMLUrlExtractor, self).__init__()
        self.feed(text)


    def handle_starttag(self, tag, attrs):
        if tag == "a":
            self.text = ""
            self.href = next((urllib.parse.urljoin(self.baseurl, attr[1]) for attr in attrs if attr[0] == "href"), None)

    def handle_endtag(self, tag):
        if self.href != None: 
            #print(self.href, self.text)
            self.urls[self.href] = self.text
        self.href = None
        self.text = None

    def handle_data(self, data):
        if self.href != None: self.text = data


def GetFirstFileTypeInDirectory(d, ext):
    files = os.listdir(d)
    return next((os.path.join(d, f) for f in files if os.path.splitext(f)[1][1:].strip().lower() == ext), None)

def GetBinary(name, url, renamepat=None):
    downloadtofile = os.path.join(TMPDIR, name + ".zip")
    bindir = os.path.join(BINPATH, name)
    if not os.path.exists(bindir): 
        if not os.path.exists(downloadtofile):
            urllib.request.urlretrieve(url, downloadtofile)
        zip_ref = zipfile.ZipFile(downloadtofile, 'r')
        extractdir = (bindir if renamepat == None else BINPATH)
        zip_ref.extractall(extractdir)
        zip_ref.close()
        os.remove(downloadtofile)
        if renamepat != None:
            os.rename(os.path.join(BINPATH, next(f for f in os.listdir(BINPATH) if f.startswith(renamepat))), bindir)
    return bindir

def GetNotepadPlusPlus():
    return os.path.join(GetBinary("notepad", URL_NOTEPAD), "notepad++.exe")

def GetVSWhere():
    binpath = os.path.join(BINPATH, "vswhere.exe")
    if os.path.exists(binpath): return binpath
    urllib.request.urlretrieve(URL_VSWHERE, binpath)
    return binpath

def GetCMAKE():
    cmake = shutil.which('cmake') 
    if cmake is not None: return cmake
    if platform.system() == "Windows":
        return os.path.join(GetBinary("cmake", URL_CMAKEZIP, "cmake-"), "bin", "cmake.exe")
    else:
        raise Exception("Please install cmake")

def GetCTEST():
    ctest = shutil.which('ctest')
    if ctest is not None: return ctest
    if platform.system() == "Windows":
        return os.path.join(GetBinary("cmake", URL_CMAKEZIP, "cmake-"), "bin", "ctest.exe")
    else:
        raise Exception("Please install cmake")

def GetImageMagick():
    return os.path.join(GetBinary("imagemagick", URL_IMAGEMAGICK), "convert.exe")

#GetImageMagick()
if platform.system() == "Windows":
    GeneratorConfig = {
        "x86dbg" : { "Generator" : EXE_GENERATOR, "Args" : ["-T", "host=x64", "-A", "Win32"], "Architecture" : "x86", "Config" : "Debug"},
        "x86rel" : { "Generator" : EXE_GENERATOR, "Args" : ["-T", "host=x64", "-A", "Win32"], "Architecture" : "x86", "Config" : "RelWithDebInfo"},
        "x64dbg" : { "Generator" : EXE_GENERATOR, "Args" : ["-T", "host=x64", "-A", "x64"], "Architecture" : "x64", "Config" : "Debug"},
        "x64rel" : { "Generator" : EXE_GENERATOR, "Args" : ["-T", "host=x64", "-A", "x64"], "Architecture" : "x64", "Config" : "RelWithDebInfo"},
        "armdbg" : { "Generator" : EXE_GENERATOR, "Args" : ["-T", "host=x64", "-A", "arm"], "Architecture" : "arm", "Config" : "Debug"},
        "armrel" : { "Generator" : EXE_GENERATOR, "Args" : ["-T", "host=x64", "-A", "arm"], "Architecture" : "arm", "Config" : "RelWithDebInfo"},
        "a64dbg" : { "Generator" : EXE_GENERATOR, "Args" : ["-T", "host=x64", "-A", "arm64"], "Architecture" : "arm64", "Config" : "Debug"},
        "a64rel" : { "Generator" : EXE_GENERATOR, "Args" : ["-T", "host=x64", "-A", "arm64"], "Architecture" : "arm64", "Config" : "RelWithDebInfo"},
        "uwp_x86dbg" : { "Generator" : UWP_GENERATOR, "Args" : ["-DCMAKE_SYSTEM_NAME=WindowsStore", "-T", "host=x64" ,"-A", "Win32"], "Architecture" : "x86", "Config" : "Debug"},
        "uwp_x86rel" : { "Generator" : UWP_GENERATOR, "Args" : ["-DCMAKE_SYSTEM_NAME=WindowsStore", "-T", "host=x64" ,"-A", "Win32"], "Architecture" : "x86", "Config" : "RelWithDebInfo"},
        "uwp_x64dbg" : { "Generator" : UWP_GENERATOR, "Args" : ["-DCMAKE_SYSTEM_NAME=WindowsStore", "-T", "host=x64" ,"-A", "x64"], "Architecture" : "x64", "Config" : "Debug"},
        "uwp_x64rel" : { "Generator" : UWP_GENERATOR, "Args" : ["-DCMAKE_SYSTEM_NAME=WindowsStore", "-T", "host=x64" ,"-A", "x64"], "Architecture" : "x64", "Config" : "RelWithDebInfo"},
        "uwp_armdbg" : { "Generator" : UWP_GENERATOR, "Args" : ["-DCMAKE_SYSTEM_NAME=WindowsStore", "-T", "host=x64" ,"-A", "arm"], "Architecture" : "arm", "Config" : "Debug"},
        "uwp_armrel" : { "Generator" : UWP_GENERATOR, "Args" : ["-DCMAKE_SYSTEM_NAME=WindowsStore", "-T", "host=x64" ,"-A", "arm"], "Architecture" : "arm", "Config" : "RelWithDebInfo"},
        "uwp_a64dbg" : { "Generator" : UWP_GENERATOR, "Args" : ["-DCMAKE_SYSTEM_NAME=WindowsStore", "-T", "host=x64" ,"-A", "arm64"], "Architecture" : "arm64", "Config" : "Debug"},
        "uwp_a64rel" : { "Generator" : UWP_GENERATOR, "Args" : ["-DCMAKE_SYSTEM_NAME=WindowsStore", "-T", "host=x64" ,"-A", "arm64"], "Architecture" : "arm64", "Config" : "RelWithDebInfo"}
    }
elif platform.system() == "Linux":
    GeneratorConfig = {
        "x86dbg" : { "Generator" : "Ninja", "Args" : [], "Architecture" : "x86", "Config" : "Debug"},
        "x86rel" : { "Generator" : "Ninja", "Args" : [], "Architecture" : "x86", "Config" : "RelWithDebInfo"},
        "x64dbg" : { "Generator" : "Ninja", "Args" : [], "Architecture" : "x64", "Config" : "Debug"},
        "x64rel" : { "Generator" : "Ninja", "Args" : [], "Architecture" : "x64", "Config" : "RelWithDebInfo"},
        "armdbg" : { "Generator" : "Ninja", "Args" : [], "Architecture" : "arm", "Config" : "Debug"},
        "armrel" : { "Generator" : "Ninja", "Args" : [], "Architecture" : "arm", "Config" : "RelWithDebInfo"},
        "a64dbg" : { "Generator" : "Ninja", "Args" : [], "Architecture" : "arm64", "Config" : "Debug"},
        "a64rel" : { "Generator" : "Ninja", "Args" : [], "Architecture" : "arm64", "Config" : "RelWithDebInfo"}
    }
    GeneratorConfig = {
        "x86dbg" : { "Args" : [], "Architecture" : "x86", "Config" : "Debug"},
        "x86rel" : { "Args" : [], "Architecture" : "x86", "Config" : "RelWithDebInfo"},
        "x64dbg" : { "Args" : [], "Architecture" : "x64", "Config" : "Debug"},
        "x64rel" : { "Args" : [], "Architecture" : "x64", "Config" : "RelWithDebInfo"},
        "armdbg" : { "Args" : [], "Architecture" : "arm", "Config" : "Debug"},
        "armrel" : { "Args" : [], "Architecture" : "arm", "Config" : "RelWithDebInfo"},
        "a64dbg" : { "Args" : [], "Architecture" : "arm64", "Config" : "Debug"},
        "a64rel" : { "Args" : [], "Architecture" : "arm64", "Config" : "RelWithDebInfo"}
    }
def GetBuildDir(dir, arch):
    return os.path.join(BUILDDIR, arch + "_" + hashlib.md5(os.path.abspath(dir).lower().encode('utf-8')).hexdigest()[0:8])

class ExecThread(threading.Thread):
    Sem = threading.BoundedSemaphore(2)
    def __init__(self, action, logfilename, cwd, command):
        threading.Thread.__init__(self)
        self.logfile = os.path.join(cwd, logfilename)
        self.action = action
        self.fhandle = None
        self.process = None
        self.command = command
        self.cwd = cwd

    def run(self):
        self.fhandle = open(self.logfile, "wb")
        print("Executing: ", self.action, "Logfile: ", self.logfile, "Directory", self.cwd)
        print("Command: ", ' '.join(self.command))
        self.process = subprocess.Popen(self.command, cwd=self.cwd, stdout=self.fhandle)
        print("Waiting for: ", self.action, "Logfile: ", self.logfile)
        rc = self.process.wait()
        if rc != 0:
            print("ErrorCode:", rc, "Logfile: ", self.logfile)
            subprocess.Popen([GetNotepadPlusPlus(), self.logfile], shell=True)
            raise Exception("Error")
        self.fhandle.close()

def ExecAndLog(action, logfilename, cwd, command):
    ExecThread.Sem.acquire()
    try:
        thrd = ExecThread(action, logfilename, cwd, command)
        thrd.start()
        thrd.join()
    finally:
        ExecThread.Sem.release()


def DoGenerate(d, arch):
    cmake = GetCMAKE()
    buildpath = GetBuildDir(d, arch)
    if not os.path.isdir(buildpath) : os.makedirs(buildpath)
    packagemanifest = GetFirstFileTypeInDirectory(d, "appxmanifest")
    command = [cmake]
    generator = None
    if "Generator" in GeneratorConfig[arch]:
        generator = GeneratorConfig[arch]["Generator"]
        command.extend(["-G", generator]) 
    if platform.system() == "Windows":
        command.extend(["-DCMAKE_SYSTEM_VERSION:STR=10.0"])
    command.extend(["-DCMAKE_CONFIGURATION_TYPES=" + GeneratorConfig[arch]["Config"]])
    if (generator == "Ninja" or generator == None):
        command.extend(["-DCMAKE_BUILD_TYPE=" + GeneratorConfig[arch]["Config"]]) 
    if "Args" in GeneratorConfig[arch]:
        command.extend(GeneratorConfig[arch]["Args"])
    command.append(os.path.join(d, "cmake"))
    command.extend(["-DBUILD_ARCH:STR=" + GeneratorConfig[arch]["Architecture"]])
    command.extend(["-DBUILD_CONFIG:STR=" + GeneratorConfig[arch]["Config"]])
    return ExecAndLog("Generate", "cmakelog.txt", buildpath, command)

def GetActions(name):
    glbs = globals().copy()
    for i in glbs:
        if not inspect.isclass(glbs[i]) or glbs[i].__name__ != name: continue
        methodnames = [m[0] for m in funcs if not m[0].startswith("__") and not m[0].endswith("__")]
        return methodnames
    return []

def LoadBuildFiles(files):
    for f in files:
        exec(open(f).read())
    glbs = globals().copy()

    projects = []
    for i in glbs:
        if inspect.isclass(glbs[i]) and glbs[i].__name__ != "Root":
            classname = glbs[i].__name__
            projects.append(classname)

def DoClean(d, arch):
    cmake = GetCMAKE()
    buildpath = GetBuildDir(d, arch)
    if os.path.isdir(buildpath): shutil.rmtree(buildpath,)

def DoBuild(d, arch):
    cmake = GetCMAKE()
    buildpath = GetBuildDir(d, arch)
    command = [cmake,  "--build", ".", "-j", "--config", GeneratorConfig[arch]["Config"]] 
    return ExecAndLog("Build", "cmakebuild.txt", buildpath, command)

def DoTest(d, arch):
    if arch != "x86dbg" and arch != "x64dbg" and arch != "x86drel" and arch != "x64rel": print("Skipping Tests on arch: ", arch)
    ctest = GetCTEST()
    buildpath = GetBuildDir(d, arch)
    command = [ctest]
    return ExecAndLog("Test", "ctest.txt", buildpath, command)

def DoInstall(d, arch):
    cmake = GetCMAKE()
    buildpath = GetBuildDir(d, arch)
    installdir = os.path.join(BUILDDIR, "install", arch)
    os.makedirs(os.path.expanduser(installdir), exist_ok=True)
    command = [cmake, "--install", "." ,  "--prefix", installdir, "--config", GeneratorConfig[arch]["Config"]] 
    return ExecAndLog("Build", "cmakeinstall.txt", buildpath, command)

def DoPackage(d, arch):
    cmake = GetCMAKE()
    buildpath = GetBuildDir(d, arch)
    command = [cmake,  "--build", buildpath, "-j", "--target", "package", "--config", GeneratorConfig[arch]["Config"]] 
    return ExecAndLog("Build", "cmakepackage.txt", buildpath, command)

def DoMakeAppx(d, arch):
    buildpath = GetBuildDir(d, arch)
    installdir = os.path.join(BUILDDIR, "install", arch)
    appxdir = os.path.join(BUILDDIR, "appx")
    os.makedirs(os.path.expanduser(appxdir), exist_ok=True)
    for f in pathlib.Path(installdir).rglob("AppxManifest.xml"): 
        appname = os.path.basename(os.path.dirname(f))
        cert = os.path.join(os.path.dirname(f), "..", appname + ".pfx")
        packfile = os.path.join(appxdir, appname + "_" + arch + ".appx")
        ExecAndLog("Appx", "makeappx.txt", appxdir, [MAKEAPPX_BINARY, "pack", "/o", "/p", packfile ,"/d", os.path.dirname(f)])
        ExecAndLog("Appx", "signtool.txt", appxdir, [SIGNTOOL_BINARY, "sign", "/fd", "SHA256", "/f", cert, packfile])

def DoOpen(d, arch):
    cmake = GetCMAKE()
    buildpath = GetBuildDir(d, arch)
    command = [cmake,  "--open", "."]
    return ExecAndLog("Open", "cmakeopen.txt", buildpath, command)

def DoLaunchVS(d, arch):
    GenerateCMakeSettings(d)
    devenv = subprocess.check_output([GetVSWhere(),  "-latest", "-prerelease", "-property", "productPath"]).decode("utf-8")
    print("Using Visual Studio: ", devenv)
    buildpath = GetBuildDir(d, arch)
    rc = subprocess.Popen([devenv, d], cwd=buildpath, shell=True)


def DoLaunchVSAppx(d, arch):
    DoLaunchVS(d, arch, True)

def DoLaunchVSExe(d, arch):
    DoLaunchVS(d, arch, False)

def DoBuildAppx(d, arch):
    DoBuild(d, arch, True)

def DoBuildExe(d, arch):
    DoBuild(d, arch, False)

def DoCleanAppx(d, arch):
    DoClean(d, arch, True)

def DoCleanExe(d, arch):
    DoClean(d, arch, False)

parser = argparse.ArgumentParser(description="Loading Script")
ActionTasks = {
    "gen" : [DoGenerate],
    "open" : [DoGenerate, DoOpen],
    "vs" : [DoGenerate, DoLaunchVS],
    "build" : [DoGenerate, DoBuild],
	"install" : [DoGenerate, DoBuild, DoInstall],
	"appx": [DoGenerate, DoBuild, DoInstall, DoMakeAppx],
	"pack" : [DoGenerate, DoPackage],
    "test" : [DoGenerate, DoPackage, DoTest],
    "clean" : [DoClean, DoClean],
    "cleanbuild" : [DoClean, DoGenerate, DoBuild]
}
archswitches = list(GeneratorConfig.keys())
archgroupswitches = ["x86", "x64", "arm", "a64", "rel", "dbg"]
actionswitches = list(ActionTasks.keys())
switches = archswitches + actionswitches + archgroupswitches

parser.add_argument("switches",
    type=str, 
    nargs='*',
    choices=switches,
    help="Switches")

def ParseArgs(opts):
    action = []
    arch = []
    for s in opts.switches:
        if (s in archswitches): arch += [s]
        if (s in actionswitches): action += [s]
        if (s in archgroupswitches): arch += [a for a in archswitches if s in a]

    if len(action) == 0: action = ["vs"]
    if len(arch) == 0: arch = ["x86dbg"]

    return {"actions" : action, "architectures" : arch}

opts = ParseArgs(parser.parse_args())
print(opts)

starttime = datetime.datetime.now()

[list([task(root, arch) for arch in opts["architectures"]]).clear() for action in opts["actions"]  for task in ActionTasks[action]]

endtime = datetime.datetime.now()

print("ElapsedTime:", endtime - starttime)
