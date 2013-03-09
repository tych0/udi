# 
# Top level SConscript file for udi projects
#
# vim: syntax=python

import sys
import os

# import utility functionality
sys.path.append(os.path.abspath('udibuild'))
import udibuild

# add command line config options
config = Variables(udibuild.GetConfigureOutput(), ARGUMENTS)
config.Add(PathVariable('UDIS86',
          'Path to the directory containing libudis86',
          None))
config.Add(PathVariable('UDIS86_INC',
          'Path to the directory for headers for libudis86',
          None))
config.Add(BoolVariable('ENABLE_OPT',
          'Build with optimization',
          False))
config.Add('MSVC_USE_SCRIPT',
           'The path to the Visual Studio vars batch',
           None)
config.Add('SCONS_PATH',
           'The path passed to the scons environment',
           None)

topenv = Environment(variables = config)

Help(config.GenerateHelpText(topenv))

if 'UDIS86' in topenv and topenv['UDIS86'] is not None:
    topenv.Append(LIBPATH = topenv['UDIS86'])

if 'UDIS86_INC' in topenv and topenv['UDIS86_INC'] is not None:
    topenv.Append(CPPPATH = topenv['UDIS86_INC'])

if 'SCONS_PATH' in topenv and topenv['SCONS_PATH'] is not None:
    topenv.AppendENVPath('PATH', topenv['SCONS_PATH'])

# configuring based on the environment
if 'configure' in COMMAND_LINE_TARGETS:
    conf = Configure(topenv)

    if udibuild.IsX86():
        if not conf.CheckLibWithHeader('udis86', 'udis86.h', 'c'):
            print 'Did not find libudis86'
            Exit(1)
    
    topenv = conf.Finish()

    config.Save(udibuild.GetConfigureOutput(), topenv)

    print 'Configure complete'
    Exit(0)

Export('topenv')

if topenv['CC'] == 'gcc':
    # C compiler flags
    topenv.Append(CFLAGS = "-Wall -Werror -fPIC -std=gnu99 -g")

    # C++ compiler flags
    topenv.Append(CXXFLAGS = "-Wall -Werror -g -fPIC")

    if 'ENABLE_OPT' in topenv and topenv['ENABLE_OPT'] is True:
        topenv.Append(CFLAGS = "-O2")
        topenv.Append(CXXFLAGS = "-O2")
    
elif topenv['CC'] == 'cl':
    pass
else:
    print 'Unknown compiler: ' + topenv['CC']
    quit()

# platform and architecture specific defines
if udibuild.IsUnix():
    topenv.Append(CPPDEFINES=['UNIX'])
else:
    topenv.Append(CPPDEFINES=['WINDOWS'])
    topenv.Append(CPPPATH = ['#/platform_inc/windows'])

if udibuild.IsLittleEndian():
    topenv.Append(CPPDEFINES=['LITTLE_ENDIAN'])
else:
    topenv.Append(CPPDEFINES=['BIG_ENDIAN'])

if udibuild.IsLinux():
    topenv.Append(CPPDEFINES=['LINUX'])

# subdirectories
topenv.SConscript('#/libudi/SConscript', variant_dir='#/build/libudi', duplicate=False)
topenv.SConscript('#/libudicommon/SConscript', variant_dir='#/build/libudicommon', duplicate=False)
topenv.SConscript('#/libudirt/SConscript', variant_dir='#/build/libudirt', duplicate=False)
topenv.SConscript('#/tests/bin/SConscript', variant_dir='#/build/tests/bin', duplicate=False)
topenv.SConscript('#/tests/libuditest/SConscript', variant_dir='#/build/tests/libuditest', duplicate=False)
topenv.SConscript('#/tests/udirt_tests/SConscript', variant_dir='#/build/tests/udirt_tests', duplicate=False)
topenv.SConscript('#/tests/udi_tests/SConscript', variant_dir='#/build/tests/udi_tests', duplicate=False)
topenv.SConscript('#/util/SConscript', variant_dir='#/build/util', duplicate=False)

# default target
topenv.Default('libudi', 'libudirt', 'tests/udirt_tests', 'tests/udi_tests', 'util')

# ctags generator
ctags = topenv.Command('ctags', '', 'ctags -R libudirt libudi libudicommon tests')

# cscope generator
cscope = topenv.Command('cscope', '', 'cscope -b -R')
