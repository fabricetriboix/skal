import sys
import os
import autodetectplf

autoplf = autodetectplf.autodetectplf()


# Command-line options

AddOption("--target", dest='target', default=autoplf,
        help="Compilation target; eg: x64-linux, arm-linux "
                "(auto-detected default: " + autoplf + ")")

AddOption("--verbose", dest='verbose', action='store_true', default=False,
        help="Display full command lines")

AddOption("--incdir", dest='incdirs', action='append', default=[],
        help="Include directory (can be specified multiple times)")

AddOption("--libdir", dest='libdirs', action='append', default=[],
        help="Library directory (can be specified multiple times)")

AddOption("--no-ccache", dest='use_ccache', action='store_false', default=True,
        help="Do not use ccache")

AddOption("--flloc", dest='use_flloc', action='store_true', default=False,
        help="Check for memory leaks using flloc")

AddOption("--mkdoc", dest='make_doc', action='store_true', default=False,
        help="Also build the documentation for all variants")


# Manage cross-compilation

tgtplf = GetOption('target')
if not tgtplf:
    tgtplf = autoplf
    print("--target not set, using auto-detected: " + tgtplf)

path = os.path.join("lib", "plf", tgtplf)
if not os.path.isdir(path) or not os.access(path, os.R_OK):
    print("ERROR: target platform not found: " + path)

sys.path.append(path)
import plfsettings


# Top-level environment

env = Environment(AR="dummy", CC="dummy", CXX="dummy", ENV={},
        HAS_DOXYGEN="no", HAS_DOT="no")

# Propagate certain environment variables
for i in ['C_INCLUDE_PATH', 'CPLUS_INCLUDE_PATH', 'LIBRARY_PATH']:
    if os.environ.has_key(i):
        env['ENV'][i] = os.environ[i]

# Re-write c++ command line without C flags
# NB: original is "$CXX -o $TARGET -c $CXXFLAGS $CCFLAGS $_CCCOMCOM $SOURCES"
env['CXXCOM'] = "$CXX -o $TARGET -c $CXXFLAGS $_CCCOMCOM $SOURCES"

if not GetOption('verbose'):
    env['CCCOMSTR']     = "CC      $TARGET"
    env['CXXCOMSTR']    = "CXX     $TARGET"
    env['ARCOMSTR']     = "AR      $TARGET"
    env['RANLIBCOMSTR'] = "RANLIB  $TARGET"
    env['LINKCOMSTR']   = "LINK    $TARGET"
    env['INSTALLSTR']   = "INSTALL $TARGET"

if GetOption('use_ccache'):
    env['CCCOM'] = "ccache " + env['CCCOM']
    env['CXXCOM'] = "ccache " + env['CXXCOM']

# Variants (NB: the first variant is the one built by default)

variantNames = ['release', 'debug']
settings = plfsettings.GetPlfSettings(variantNames)
variants = {}

for v in variantNames:
    variants[v] = {}
    variants[v]['target'] = tgtplf
    variants[v]['topdir'] = os.getcwd()

    path = os.path.abspath(os.path.join("build", tgtplf, v))
    variants[v]['build_root'] = path
    variants[v]['build_inc'] = os.path.join(path, "include")
    variants[v]['build_doc'] = os.path.join(path, "doc")

    variants[v]['env'] = env.Clone()
    variants[v]['env']['CC'] = settings[v]['cc']
    variants[v]['env']['CXX'] = settings[v]['cxx']
    variants[v]['env']['AR'] = settings[v]['ar']
    variants[v]['env']['RANLIB'] = settings[v]['ranlib']
    variants[v]['env'].AppendENVPath('PATH', settings[v]['path'])
    variants[v]['env'].Append(CPPDEFINES = settings[v]['cppdefines'])
    variants[v]['env'].Append(CCFLAGS = settings[v]['ccflags'])
    variants[v]['env'].Append(CXXFLAGS = settings[v]['cxxflags'])
    variants[v]['env'].Append(CPPPATH = settings[v]['cpppath'])
    variants[v]['env'].Append(LINKFLAGS = settings[v]['linkflags'])
    variants[v]['env'].Append(LIBPATH = settings[v]['libpath'])
    variants[v]['env'].Append(LIBPATH = [variants[v]['build_root']])

    # Add include and library paths specified on the command line
    for d in GetOption('incdirs'):
        variants[v]['env'].Append(CPPPATH = os.path.abspath(d))
    for d in GetOption('libdirs'):
        variants[v]['env'].Append(LIBPATH = os.path.abspath(d))

    # Autoconf-like stuff
    if not GetOption('clean') and not GetOption('help'):
        conf = Configure(variants[v]['env'])

        hasDoxy = False
        hasDot = False
        if not conf.CheckProg("doxygen"):
            print("doxygen not found; doxygen documentation will not be generated")
        else:
            hasDoxy = True
            if not conf.CheckProg("dot"):
                print("dot not found; doxygen documentation will not have graphs")
            else:
                hasDot = True

        if GetOption('use_ccache') and not conf.CheckProg("ccache"):
            print("ERROR ccache not found")
            Exit(1)

        if not conf.CheckCC():
            print("ERROR C compiler not found: " + variants[v]['env']['CC'])
            Exit(1)

        variants[v]['env'] = conf.Finish()

        # Update the environment after calling `conf.Finish()`
        if hasDoxy:
            variants[v]['env']['HAS_DOXYGEN'] = "yes"
            if hasDot:
                variants[v]['env']['HAS_DOT'] = "yes"

# Include SConscript for each variant
for v in variantNames:
    SConscript(os.path.join("lib", "SConscript"),
            variant_dir=variants[v]['build_root'],
            duplicate=0, exports={'variant': variants[v]})

# Manage targets
alltgt = []
for v in variantNames:
    Alias(v, variants[v]['build_root'])
    alltgt.append(variants[v]['build_root'])

Alias('all', alltgt)
Default(variantNames[0])
