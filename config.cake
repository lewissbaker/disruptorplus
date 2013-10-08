###############################################################################
# DisruptorPlus - C++ Library implementing a Disruptor data-structure
# Copyright (c) 2013  Lewis Baker
###############################################################################

import sys
import cake.path
import cake.system

from cake.engine import Variant
from cake.script import Script

configuration = Script.getCurrent().configuration
engine = configuration.engine

hostPlatform = cake.system.platform().lower()
hostArchitecture = cake.system.architecture().lower()

from cake.library.script import ScriptTool
from cake.library.variant import VariantTool
from cake.library.env import EnvironmentTool
from cake.library.project import ProjectTool
from cake.library.compilers import CompilerNotFoundError
from cake.library.compilers.msvc import findMsvcCompiler

# Setup all of the parameters common to all variants
baseVariant = Variant()
baseVariant.tools["script"] = script = ScriptTool(configuration=configuration)
baseVariant.tools["variant"] = variant = VariantTool(configuration=configuration)
baseVariant.tools["env"] = env = EnvironmentTool(configuration=configuration)
env["DISRUPTOR"] = "."
env["VARIANT"] = "${PLATFORM}_${ARCHITECTURE}_${COMPILER}${COMPILER_VERSION}_${RELEASE}"
env["BUILD"] = "build/${VARIANT}"
env["DISRUPTOR_BUILD"] = "${BUILD}"
env["DISRUPTOR_PROJECT"] = "build/project"
env["DISRUPTOR_BIN"] = "${DISRUPTOR_BUILD}/bin"
env["DISRUPTOR_LIB"] = "${DISRUPTOR_BUILD}/lib"
baseVariant.tools["project"] = project = ProjectTool(configuration=configuration)
project.product = project.VS2010
project.enabled = engine.options.createProjects
if project.enabled:
  engine.addBuildSuccessCallback(project.build)

def createVariants(baseVariant):
  for release in ["debug", "optimised"]:
    variant = baseVariant.clone(release=release)

    platform = variant.keywords["platform"]
    compilerName = variant.keywords["compiler"]
    architecture = variant.keywords["architecture"]

    env = variant.tools["env"]
    env["RELEASE"] = release
    env["BUILD"] = env.expand("${BUILD}")

    compiler = variant.tools["compiler"]
    compiler.enableRtti = True
    compiler.enableExceptions = True
    compiler.outputMapFile = True
    compiler.messageStyle = compiler.MSVS_CLICKABLE

    if release == "debug":
      compiler.addDefine("_DEBUG")
      compiler.debugSymbols = True
      compiler.useIncrementalLinking = True
      compiler.optimisation = compiler.NO_OPTIMISATION
      compiler.runtimeLibraries = 'debug-dll'
    elif release == "optimised":
      compiler.addDefine("NDEBUG")
      compiler.debugSymbols = True
      compiler.useIncrementalLinking = False
      compiler.useFunctionLevelLinking = True
      compiler.optimisation = compiler.FULL_OPTIMISATION
      compiler.runtimeLibraries = 'release-dll'
      compiler.addCppFlag('/Oxs')

    # Disable the compiler during project generation
    if engine.options.createProjects:
      compiler.enabled = False

    projectTool = variant.tools["project"]
    projectTool.projectConfigName = "%s (%s) %s (%s)" % (
      platform.capitalize(),
      architecture,
      release.capitalize(),
      compilerName,
      )
    projectTool.projectPlatformName = "Win32"

    projectTool.solutionConfigName = release.capitalize()
    projectTool.solutionPlatformName = "%s %s (%s)" % (
      platform.capitalize(),
      compilerName.capitalize(),
      architecture,
      )

    configuration.addVariant(variant)

if cake.system.isWindows() or cake.system.isCygwin():
  for msvcVer in ("11.0",):
    for arch in ("x86", "x64"):
      try:
        msvcVariant = baseVariant.clone(
          platform="windows",
          compiler="msvc" + msvcVer,
          compilerFamily="msvc",
          architecture=arch,
          )
        msvcVariant.tools["compiler"] = compiler = findMsvcCompiler(
          configuration=configuration,
          architecture=arch,
          version=msvcVer,
          )
        compiler.addDefine("WIN32")
        compiler.addDefine("_WIN32_WINNT", "0x0500") # WinXP
        if arch in ("x64", "ia64"):
          compiler.addDefine("WIN64")

        env = msvcVariant.tools["env"]
        env["COMPILER"] = "msvc"
        env["COMPILER_VERSION"] = msvcVer
        env["PLATFORM"] = "windows"
        env["ARCHITECTURE"] = arch

        createVariants(msvcVariant)
      except CompilerNotFoundError:
         pass

else:
  configuration.engine.raiseError("Unsupported platform: %s\n" % cake.system.platform())
