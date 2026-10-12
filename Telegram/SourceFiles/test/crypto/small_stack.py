import resource
import subprocess
import sys


def limit_stack():
    soft, hard = resource.getrlimit(resource.RLIMIT_STACK)
    limit = 1024 * 1024
    if hard != resource.RLIM_INFINITY:
        limit = min(limit, hard)
    resource.setrlimit(resource.RLIMIT_STACK, (limit, hard))
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


result = subprocess.run(
    [sys.argv[1], "--case", "vault/stream-boundaries"],
    preexec_fn=limit_stack,
    check=False,
)
if result.returncode < 0:
    print(f"FAIL: Vault terminated by signal {-result.returncode} with 1 MiB stack")
sys.exit(0 if result.returncode == 0 else 1)
