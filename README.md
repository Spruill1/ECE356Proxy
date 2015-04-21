Contributors:   David Spruill
                Elder Yoshida

To compile:     gcc -pthread -o proxy proxy.c

What works:     It should be meeting all project goals, it can act as a proxy to get to a website and will cache the incoming data.
                Later accesses will search through the cache before they attempt to read from the server again.  We were able to
                prove our caching works by loading the page through the proxy, then disconnecting internet from our machine, closing
                Firefox, re-opening Firefox, clearing the browser cache (Preferences->advanced->network) and then inputting the same
                url as before - the page will load.

Quirks:         To maintain concurrency we lock down the entire cache for every write to it.  So it can take a while for everything
                to be cached.  The website will be forwarded to the browser immediately, but it won't be cached yet - wait a minute
                or so (updates will appear on the command line)
                We have it so that no overly large objects will be cached to help speed up the program.  This means that loading a
                cached page might still have to grab those files from the internet.  The if statement running this is on line 415.
