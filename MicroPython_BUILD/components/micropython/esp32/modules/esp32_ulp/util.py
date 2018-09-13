DEBUG = False

import gc


def garbage_collect(msg, verbose=DEBUG):
    free_before = gc.mem_free()
    gc.collect()
    free_after = gc.mem_free()
    if verbose:
        print("%s: %d --gc--> %d bytes free" % (msg, free_before, free_after))
