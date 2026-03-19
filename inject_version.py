Import("env")

version = env.GetProjectOption("custom_firmware_version", "0.0.0")
env.Append(CPPDEFINES=[("FIRMWARE_VERSION", env.StringifyMacro(version))])
