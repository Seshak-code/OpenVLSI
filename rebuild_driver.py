import re

with open("/tmp/local_worker_driver.cpp.txt", "r") as f:
    lines = f.readlines()

# Strip line numbers
lines = [re.sub(r'^\s*\d+\t', '', line) for line in lines]

header = "".join(lines[0:22])
helpers = "".join(lines[42:77])
impl_struct = "".join(lines[87:122]).replace("LocalWorkerDriver::Impl::Impl", "LocalWorkerDriver::Impl")
methods = "".join(lines[123:588]).replace("LocalWorkerDriver::Impl::Impl::", "LocalWorkerDriver::Impl::")
delegations = "".join(lines[639:661])
footer = "".join(lines[661:665])

final_content = header + "\nnamespace acutesim {\nnamespace compute {\nnamespace orchestration {\n\n" + helpers + "\n" + impl_struct + "\n" + methods + "\n" + delegations + "\n" + footer

with open("compute/orchestration/driver/local_worker_driver.cpp", "w") as f:
    f.write(final_content)
