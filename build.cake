###############################################################################
# C++ Disruptor Library
# Copyright (c) 2013  Lewis Baker
###############################################################################

from cake.tools import script

script.execute(script.cwd([
  "benchmark/build.cake",
  "test/build.cake",
  ]))
