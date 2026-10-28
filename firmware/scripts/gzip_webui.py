# ruff: noqa: F821
import gzip
import os
import shutil

Import("env")

FS_TARGETS = {"buildfs", "uploadfs", "uploadfsota"}

if FS_TARGETS & set(COMMAND_LINE_TARGETS):
    src = os.path.join(env.subst("$PROJECT_DIR"), "data")
    out = os.path.join(env.subst("$BUILD_DIR"), "webui")
    shutil.rmtree(out, ignore_errors=True)
    for root, _, files in os.walk(src):
        rel = os.path.relpath(root, src)
        dst = os.path.join(out, rel)
        os.makedirs(dst, exist_ok=True)
        for name in files:
            with open(os.path.join(root, name), "rb") as f:
                data = f.read()
            # mtime=0 keeps the output (and its ETag) identical for identical sources
            with open(os.path.join(dst, name + ".gz"), "wb") as f:
                f.write(gzip.compress(data, compresslevel=9, mtime=0))
    print("Web UI: gzipped %s -> %s" % (src, out))
    env.Replace(PROJECT_DATA_DIR=out)
