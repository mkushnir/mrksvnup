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
RA protocol][1], and the [svndiff1][2] editor. The supported protocols are
currently restricted to the _svn://_ only. The utility behaves more
like a traditional svn client.  It tracks the latest checked out revision
in a dotfile in the root directory.  It then can update to a different
revision _relative to the curernt one_ using svndiff documents downloaded
from the server. It doesn't delete the files not being tracked on the
remote side (for example, it won't wipe out custom kernel confurations
during update).  However, unlike the traditional svn client, it doesn't
track each insividual file's state which is one of my TODO's. Without this
function (at least caching the file checksum) the utility won't be able to
determine a locally modified file that was not modified between revisions,
and thus was not included in the svndiff document. An alternative solution
is to query each file's checksum on the server. It would slow down
updating large repositories and is not considered for now.

This program was first written as a proof of concept, mostly in order to
explore the design decision. It can, however, be used in real world tasks.

The program supports the _svn://_ protocol only. Since this is my
homework, I made no provision for distributing it through ports, or other
packaging systems. It can be built using basic [GNU autotools][4].

TODO
====

* Cache file checksums locally (/var/run/svnup/<localpath>).
* Strict (the -f option) and loose (default) update semantics.
* Diagnostics, verbosity, etc.
* Support of different transports: http://, https:// svn+ssh://.
* Portability to other platforms (?).


[1]: http://svn.apache.org/repos/asf/subversion/trunk/subversion/libsvn_ra_svn/protocol "RA SVN Protocol Specification"
[2]: http://svn.apache.org/repos/asf/subversion/trunk/notes/svndiff
[3]: https://metacpan.org/module/Parse::SVNDiff
[4]: http://en.wikipedia.org/wiki/GNU_build_system 
