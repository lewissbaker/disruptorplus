from cake.tools import script, env, compiler

compiler.addIncludePath(env.expand("${DISRUPTOR}/include"))

test1 = compiler.program(
  target=env.expand("${DISRUPTOR_BUILD}/test_1"),
  sources=compiler.objects(
    targetDir=env.expand("${DISRUPTOR_BUILD}"),
    sources=script.cwd([
      "test_1.cpp",
      ])),
  )
test2 = compiler.program(
  target=env.expand("${DISRUPTOR_BUILD}/test_2"),
  sources=compiler.objects(
    targetDir=env.expand("${DISRUPTOR_BUILD}"),
    sources=script.cwd([
      "test_2.cpp",
      ])),
  )
