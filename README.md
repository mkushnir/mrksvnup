mrksvnup - A Fast _svnup_ For FreeBSD.
======================================


This program was inspired by pioneering work of John Mehr: [the svnup
utility](http://jcm.dsl.visi.com/freebsd/svnup/), and an earlier
Dag-Erling Smørgrav's experiment:
[svnsup](http://svnweb.freebsd.org/base/user/des/svnsup/).  My intention
was to resolve some John's utility's problems of resource usage found by
the time of this writing (hopefully it will be improved).  I also wanted
to achieve a faster run time in at least the obvious cases of small
incremental updates.  The John's utility is based on the _main command
set_ of the SVN RA protocol.  This approach, while yields a correct net
result, is probably not the most optimal in terms of performance. It
requires walking through the entire tree regardless of whether it's
actually needed or not, on the local side.  It doesn't make benefit from
on-the-wire compression normally offered by SVN.

The idea was to see how the [official svn](http://subversion.apache.org/)
client completes some simple usage senarios: checking out a fresh tree,
updating between _source_ and _target_ revisions. The solution I think was
in the direction of Dag-Erling's experiment.

_mrksvnup_ includes a simplified (and limited) implementation of the [SVN
RA protocol][1], and the [svndiff1][2] editor. The supported transports are
currently restricted to the _svn://_ only. The utility behaves more
like a traditional svn client.  It tracks the latest checked out revision
in a dotfile in the root directory.  It then can update to a different
revision _relative to the curernt one_ using svndiff documents downloaded
from the server. It doesn't delete the files not being tracked on the
remote side (for example, it won't wipe out custom kernel confurations
during update).

The _mrksvnup_ utility keeps some  minimal state about each tracked file:
its remote path and the checksum as a key/value pair in a local database.
An update procedure is carried out in two stages: first an _svndiff_
document is obtained given the source and target revisions, and the files
are edited along with this document.  During this stage each file's remote
path and checksum is saved in the database. When someting goes wrong,
a full copy of an individual file is checked out from the server using the
_get-file_ svn command.

The next stage is checking local tree against the full database. During
this stage the known tracked files that might possibly be deleted/modified
locally, but not listed remotely in the svndiff document (because they
wouldn't change) are restored using the _get-file_ command.

This program was first written as a proof of concept, mostly in order to
explore the design decision. It can, however, be used in real world tasks.
Since this is my homework, I made no provision for distributing it through
ports, or other packaging systems. It can be built using basic [GNU
autotools][4].

TODO
====

* Cache file checksums locally (/var/run/svnup/\[localpath\]).
* Strict (the -f option) and loose (default) update semantics.
* Diagnostics, verbosity, etc.
* Support of different transports: http://, https:// svn+ssh://.
* Portability to other platforms (?).


[1]: http://svn.apache.org/repos/asf/subversion/trunk/subversion/libsvn_ra_svn/protocol "RA SVN Protocol Specification"
[2]: http://svn.apache.org/repos/asf/subversion/trunk/notes/svndiff
[3]: https://metacpan.org/module/Parse::SVNDiff
[4]: http://en.wikipedia.org/wiki/GNU_build_system 
