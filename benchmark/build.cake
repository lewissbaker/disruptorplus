from cake.tools import script, env, compiler

compiler.addIncludePath(env.expand("${DISRUPTOR}/include"))

benchmarks = ["unicast",
              "multicast",
              "sequencer",
              "pipeline",
              "diamond",
              ]

programs = []
for benchmark in benchmarks:
  programs.append(
    compiler.program(
        target=env.expand("${DISRUPTOR_BUILD}/" + benchmark),
        sources=compiler.objects(
          targetDir=env.expand("${DISRUPTOR_BUILD}"),
          sources=script.cwd([benchmark + ".cpp"]),
          )))
