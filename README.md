# File-Searcher
C code for searching files by a given term, using pthread's multi-threading.
Run as './executable [search-root-directory] [search-term] [number-of-searching-threads(workers)]', 
and the code will search for files with names that include the 'search-term', starting the the 'search-root-directory', working with the mentioned number of 
threads, i.e workers (mentioned at 'number-of-searching-threads').
