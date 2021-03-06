#!/usr/bin/python
# Copyright 2013 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Downloads, builds (with instrumentation) and installs shared libraries."""

import argparse
import os
import platform
import shutil
import subprocess
import sys

# Build parameters for different sanitizers.
# We use XORIGIN as RPATH and after building library replace it to $ORIGIN
# The reason: this flag goes through configure script and makefiles
# differently for different libraries. So the dollar sign '$' should be
# differently escaped. Instead of having problems with that it just
# uses XORIGIN to build library and after that replaces it to $ORIGIN
# directly in .so file.
SUPPORTED_SANITIZERS = {
    'asan': {
        'compiler_flags': '-O2 -fsanitize=address -gline-tables-only -fPIC -w '
                          '-U_FORITFY_SOURCE',
        'linker_flags': '-fsanitize=address -Wl,-z,origin -Wl,-R,XORIGIN/.'
    },
    'msan': {
        'compiler_flags': '-O2 -fsanitize=memory '
                          '-fsanitize-memory-track-origins '
                          '-gline-tables-only -fPIC -w -U_FORTIFY_SOURCE',
        'linker_flags': '-fsanitize=memory -Wl,-z,origin -Wl,-R,XORIGIN/.'
    },
    'tsan': {
        'compiler_flags': '-O2 -fsanitize=thread -gline-tables-only -fPIC -w '
                          '-U_FORTIFY_SOURCE',
        'linker_flags': '-fsanitize=thread -Wl,-z,origin -Wl,-R,XORIGIN/.'
    },
}


class ScopedChangeDirectory(object):
  """Changes current working directory and restores it back automatically."""

  def __init__(self, path):
    self.path = path
    self.old_path = ''

  def __enter__(self):
    self.old_path = os.getcwd()
    os.chdir(self.path)
    return self

  def __exit__(self, exc_type, exc_value, traceback):
    os.chdir(self.old_path)


def get_script_absolute_path():
  return os.path.dirname(os.path.abspath(__file__))


def get_library_build_dependencies(library):
  command = 'apt-get -s build-dep %s | grep Inst | cut -d " " -f 2' % library
  command_result = subprocess.Popen(command, stdout=subprocess.PIPE,
                                    shell=True)
  if command_result.wait():
    raise Exception('Failed to determine build dependencies for %s' % library)
  build_dependencies = [l.strip() for l in command_result.stdout]
  return build_dependencies


def check_library_build_dependencies(library):
  build_dependencies = get_library_build_dependencies(library)
  if len(build_dependencies):
    print >> sys.stderr, 'Please, install build-dependencies for %s' % library
    print >> sys.stderr, 'One-liner for APT:'
    print >> sys.stderr, 'sudo apt-get -y --no-remove build-dep %s' % library
    sys.exit(1)


def shell_call(command, verbose=False, environment=None):
  """ Wrapper on subprocess.Popen

  Calls command with specific environment and verbosity using
  subprocess.Popen

  Args:
    command: Command to run in shell.
    verbose: If False, hides all stdout and stderr in case of successful build.
        Otherwise, always prints stdout and stderr.
    environment: Parameter 'env' for subprocess.Popen.

  Returns:
    None

  Raises:
    Exception: if return code after call is not zero.
  """
  child = subprocess.Popen(
      command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
      env=environment, shell=True)
  stdout, stderr = child.communicate()
  if verbose or child.returncode:
    print stdout
  if child.returncode:
    raise Exception('Failed to run: %s' % command)


def run_shell_commands(commands, verbose=False, environment=None):
  for command in commands:
    shell_call(command, verbose, environment)


def destdir_configure_make_install(parsed_arguments, environment,
                                   install_prefix):
  configure_command = './configure %s' % parsed_arguments.extra_configure_flags
  configure_command += ' --libdir=/lib/'
  # Installing to a temporary directory allows us to safely clean up the .la
  # files below.
  destdir = '%s/debian/instrumented_build' % os.getcwd()
  # Some makefiles use BUILDROOT instead of DESTDIR.
  make_command = 'make -j%s DESTDIR=%s BUILDROOT=%s' % (
      parsed_arguments.jobs, destdir, destdir),
  run_shell_commands([
      configure_command,
      make_command,
      '%s install' % make_command,
      # Kill the .la files. They contain absolute paths, and will cause build
      # errors in dependent libraries.
      'rm %s/lib/*.la -f' % destdir,
      # Now move the contents of the temporary destdir to their final place.
      'cp %s/* %s/ -rdf' % (destdir, install_prefix)],
                     parsed_arguments.verbose, environment)


def prefix_configure_make_install(parsed_arguments, environment,
                                  install_prefix):
  configure_command = './configure %s --prefix=%s' % (
      parsed_arguments.extra_configure_flags, install_prefix)
  shell_call(configure_command, parsed_arguments.verbose, environment)
  shell_call('make -j%s' % parsed_arguments.jobs,
             parsed_arguments.verbose, environment)
  shell_call('make -j%s install' % parsed_arguments.jobs,
             parsed_arguments.verbose, environment)


def nss_make_and_copy(parsed_arguments, environment, install_prefix):
  # NSS uses a build system that's different from configure/make/install. All
  # flags must be passed as arguments to make.
  make_args = []
  # Do an optimized build.
  make_args.append('BUILD_OPT=1')
  # Set USE_64=1 on x86_64 systems.
  if platform.architecture()[0] == '64bit':
    make_args.append('USE_64=1')
  # Passing C(XX)FLAGS overrides the defaults, and EXTRA_C(XX)FLAGS is not
  # supported. Append our extra flags to CC/CXX.
  make_args.append('CC="%s %s"' % (environment['CC'], environment['CFLAGS']))
  make_args.append('CXX="%s %s"' %
                   (environment['CXX'], environment['CXXFLAGS']))
  # We need to override ZDEFS_FLAGS at least to prevent -Wl,-z,defs.
  # Might as well use this to pass the linker flags, since ZDEF_FLAGS is always
  # added during linking on Linux.
  make_args.append('ZDEFS_FLAG="-Wl,-z,nodefs %s"' % environment['LDFLAGS'])
  make_args.append('NSPR_INCLUDE_DIR=/usr/include/nspr')
  make_args.append('NSPR_LIB_DIR=%s/lib' % install_prefix)
  make_args.append('NSS_ENABLE_ECC=1')
  with ScopedChangeDirectory('nss') as cd_nss:
    # -j is not supported
    shell_call('make %s' % ' '.join(make_args), parsed_arguments.verbose,
               environment)
    # 'make install' is not supported. Copy the DSOs manually.
    install_dir = '%s/lib/' % install_prefix
    for (dirpath, dirnames, filenames) in os.walk('./lib/'):
      for filename in filenames:
        if filename.endswith('.so'):
          full_path = os.path.join(dirpath, filename)
          if parsed_arguments.verbose:
            print 'download_build_install.py: installing %s' % full_path
          shutil.copy(full_path, install_dir)


def libcap2_make_install(parsed_arguments, environment, install_prefix):
  # libcap2 doesn't come with a configure script
  make_args = [
      '%s="%s"' % (name, environment[name])
      for name in['CC', 'CXX', 'CFLAGS', 'CXXFLAGS', 'LDFLAGS']]
  shell_call('make -j%s %s' % (parsed_arguments.jobs, ' '.join(make_args)),
             parsed_arguments.verbose, environment)
  install_args = [
      'DESTDIR=%s' % install_prefix,
      # Do not install in lib64/.
      'lib=lib',
      # Skip a step that requires sudo.
      'RAISE_SETFCAP=no'
  ]
  shell_call('make -j%s install %s' %
             (parsed_arguments.jobs, ' '.join(install_args)),
             parsed_arguments.verbose, environment)


def libpci3_make_install(parsed_arguments, environment, install_prefix):
  # pciutils doesn't have a configure script
  # This build script follows debian/rules.

  # `make install' will create a "$(DESTDIR)-udeb" directory alongside destdir.
  # We don't want that in our product dir, so we use an intermediate directory.
  destdir = '%s/debian/pciutils' % os.getcwd()
  make_args = [
      '%s="%s"' % (name, environment[name])
      for name in['CC', 'CXX', 'CFLAGS', 'CXXFLAGS', 'LDFLAGS']]
  make_args.append('SHARED=yes')
  paths = [
      'LIBDIR=/lib/',
      'PREFIX=/usr',
      'SBINDIR=/usr/bin',
      'IDSDIR=/usr/share/misc',
  ]
  install_args = ['DESTDIR=%s' % destdir]
  run_shell_commands([
      'mkdir -p %s-udeb/usr/bin' % destdir,
      'make -j%s %s' % (parsed_arguments.jobs, ' '.join(make_args + paths)),
      'make -j%s %s install' % (
          parsed_arguments.jobs,
          ' '.join(install_args + paths))],
                     parsed_arguments.verbose, environment)
  # Now move the contents of the temporary destdir to their final place.
  run_shell_commands([
      'cp %s/* %s/ -rd' % (destdir, install_prefix),
      'install -m 644 lib/libpci.so* %s/lib/' % install_prefix,
      'ln -sf libpci.so.3.1.8 %s/lib/libpci.so.3' % install_prefix],
                     parsed_arguments.verbose, environment)


def build_and_install(parsed_arguments, environment, install_prefix):
  if parsed_arguments.build_method == 'destdir':
    destdir_configure_make_install(
        parsed_arguments, environment, install_prefix)
  elif parsed_arguments.build_method == 'prefix':
    prefix_configure_make_install(parsed_arguments, environment, install_prefix)
  elif parsed_arguments.build_method == 'custom_nss':
    nss_make_and_copy(parsed_arguments, environment, install_prefix)
  elif parsed_arguments.build_method == 'custom_libcap':
    libcap2_make_install(parsed_arguments, environment, install_prefix)
  elif parsed_arguments.build_method == 'custom_pango':
    parsed_arguments.extra_configure_flags += \
      ' --x-libraries=%s/lib' % install_prefix
    parsed_arguments.extra_configure_flags += \
      ' --x-includes=%s/include' % install_prefix
    prefix_configure_make_install(parsed_arguments, environment, install_prefix)
  elif parsed_arguments.build_method == 'custom_libpci3':
    libpci3_make_install(parsed_arguments, environment, install_prefix)
  elif parsed_arguments.build_method == 'custom_libappindicator1':
    environment['CSC'] = '/usr/bin/mono-csc'
    destdir_configure_make_install(
        parsed_arguments, environment, install_prefix)
  else:
    raise Exception('Unrecognized build method: %s' %
                    parsed_arguments.build_method)


def download_build_install(parsed_arguments):
  sanitizer_params = SUPPORTED_SANITIZERS[parsed_arguments.sanitizer_type]

  environment = os.environ.copy()
  # Usage of environment variables CC and CXX prefers usage flags --c-compiler
  # and --cxx-compiler
  if 'CC' not in environment and parsed_arguments.cc:
    environment['CC'] = parsed_arguments.cc
  if 'CXX' not in environment and parsed_arguments.cxx:
    environment['CXX'] = parsed_arguments.cxx

  product_directory = os.path.normpath('%s/%s' % (
      get_script_absolute_path(),
      parsed_arguments.product_directory))

  compiler_flags = sanitizer_params['compiler_flags']
  if parsed_arguments.sanitizer_blacklist:
    compiler_flags += ' -fsanitize-blacklist=%s/%s' % (
        product_directory,
        parsed_arguments.sanitizer_blacklist)
  environment['CFLAGS'] = '%s %s' % (compiler_flags,
                                     parsed_arguments.extra_cflags)
  environment['CXXFLAGS'] = '%s %s' % (
      compiler_flags,
      parsed_arguments.extra_cxxflags)

  install_prefix = '%s/instrumented_libraries/%s' % (
      product_directory,
      parsed_arguments.sanitizer_type)

  # Make sure the linker searches the instrumented libraries dir for
  # library dependencies.
  environment['LDFLAGS'] = '%s -L%s/lib %s' % (
      sanitizer_params['linker_flags'],
      install_prefix, parsed_arguments.extra_ldflags)

  library_directory = '%s/%s' % (parsed_arguments.intermediate_directory,
                                 parsed_arguments.library)

  # A failed build might have left a dirty source tree behind.
  if os.path.exists(library_directory):
    shell_call('rm -rf %s' % library_directory, parsed_arguments.verbose)
  os.makedirs(library_directory)

  with ScopedChangeDirectory(library_directory) as cd_library:
    shell_call('apt-get source %s' % parsed_arguments.library,
               parsed_arguments.verbose)
    # There should be exactly one subdirectory after downloading a package.
    subdirectories = [d for d in os.listdir('.') if os.path.isdir(d)]
    if len(subdirectories) != 1:
      raise (Exception('There was not one directory after downloading '
                       'a package %s' % parsed_arguments.library))
    with ScopedChangeDirectory(subdirectories[0]):
      # Here we are in the package directory.
      if parsed_arguments.run_before_build:
        shell_call(
            '%s/%s' %
            (os.path.relpath(cd_library.old_path),
             parsed_arguments.run_before_build),
            parsed_arguments.verbose)
      try:
        build_and_install(parsed_arguments, environment, install_prefix)
      except Exception as exception:
        print exception
        print 'Failed to build library %s.' % parsed_arguments.library
        print ('Probably, some of its dependencies are not installed: %s' %
               ' '.join(get_library_build_dependencies(parsed_arguments.library)))
        sys.exit(1)

  # Touch a txt file to indicate library is installed.
  open('%s/%s.txt' % (install_prefix, parsed_arguments.library), 'w').close()

  # Remove downloaded package and generated temporary build files.
  # Failed builds intentionally skip this step, in order to aid in tracking down
  # build failures.
  shell_call('rm -rf %s' % library_directory, parsed_arguments.verbose)


def main():
  argument_parser = argparse.ArgumentParser(
      description='Download, build and install instrumented library')

  argument_parser.add_argument('-j', '--jobs', type=int, default=1)
  argument_parser.add_argument('-l', '--library', required=True)
  argument_parser.add_argument(
      '-i', '--product-directory', default='.',
      help='Relative path to the directory with chrome binaries')
  argument_parser.add_argument(
      '-m', '--intermediate-directory', default='.',
      help='Relative path to the directory for temporary build files')
  argument_parser.add_argument('--extra-configure-flags', default='')
  argument_parser.add_argument('--extra-cflags', default='')
  argument_parser.add_argument('--extra-cxxflags', default='')
  argument_parser.add_argument('--extra-ldflags', default='')
  argument_parser.add_argument('-s', '--sanitizer-type', required=True,
                               choices=SUPPORTED_SANITIZERS.keys())
  argument_parser.add_argument('-v', '--verbose', action='store_true')
  argument_parser.add_argument('--check-build-deps', action='store_true')
  argument_parser.add_argument('--cc')
  argument_parser.add_argument('--cxx')
  # This should be a shell script to run before building specific libraries
  # e.g. extracting archives with sources, patching makefiles, etc.
  argument_parser.add_argument('--run-before-build', default='')
  argument_parser.add_argument('--build-method', default='destdir')
  argument_parser.add_argument('--sanitizer-blacklist', default='')

  # Ignore all empty arguments because in several cases gyp passes them to the
  # script, but ArgumentParser treats them as positional arguments instead of
  # ignoring (and doesn't have such options).
  parsed_arguments = argument_parser.parse_args(
      [arg for arg in sys.argv[1:] if len(arg) != 0])
  # Ensure current working directory is this script directory.
  os.chdir(get_script_absolute_path())
  # Ensure all build dependencies are installed.
  if parsed_arguments.check_build_deps:
    check_library_build_dependencies(parsed_arguments.library)

  download_build_install(parsed_arguments)


if __name__ == '__main__':
  main()
