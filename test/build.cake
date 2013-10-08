from cake.tools import script, env, compiler

compiler.addIncludePath(env.expand("${DISRUPTOR}/include"))

objects = compiler.objects(
  targetDir=env.expand("${DISRUPTOR_BUILD}"),
  sources=script.cwd([
    "test_1.cpp",
    ]))

program = compiler.program(
  target=env.expand("${DISRUPTOR_BUILD}/test_1"),
  sources=objects,
  )  
