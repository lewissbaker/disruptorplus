from cake.tools import script, env, compiler

compiler.addIncludePath(env.expand("${DISRUPTOR}/include"))

def buildProgram(program):
  return compiler.program(
    target=env.expand("${DISRUPTOR_BUILD}/" + program),
    sources=compiler.objects(
      targetDir=env.expand("${DISRUPTOR_BUILD}"),
      sources=script.cwd([program + ".cpp"]),
      ))

benchmarkSingle = buildProgram("benchmark")
test2 = buildProgram("test_2")
