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
set_ of the SVN RA protocol.  This approach, while yields the correct net
result, is probably not the most optimal in terms of performance. It
requires walking through the entire tree regardless of whether it's
actually needed or not, on the local side.  It doesn't make benefit from
on-the-wire compression normally offered by SVN.

The idea was to see how the [official svn](http://subversion.apache.org/)
client completes some simple usage scenarios: checking out a fresh tree,
updating between _source_ and _target_ revisions. The solution I think was
in the direction of Dag-Erling's experiment.

_mrksvnup_ includes a simplified (and limited) implementation of the [SVN
RA protocol][1], [WebDAV][6], and the [svndiff1][2] editor. The supported
transports are currently restricted to the _svn://_ and _http://_ only.
The utility behaves more like a traditional svn client.  It tracks the
latest checked out revision in a dotfile in the root directory.  It then
can update to a different revision _relative to the current one_ using
svndiff documents downloaded from the server. When it's done on a regular
basis, it's really fast. The utility doesn't delete files not being
tracked on the remote side (for example, it won't wipe out custom kernel
configurations during update) unless the containing directory was removed
on the server.

During an update procedure the server "pushes" _svndiff_ documents to the
client given the source and target revisions. The client modifies the
corresponding files along with these documents.  When something went
wrong, a full copy of an individual file is checked out from the server
using the _get-file_ svn command.  Each file's remote path and final
checksum is saved in the database.

The _mrksvnup_ utility can also repair a corrupted tree (to some extent)
using some minimal state about each tracked file: its remote path and the
checksum as a key/value pair saved in a local database during update.
Repairment stage can be entered at an option, and performs a double check
of the local tree against the hash database. During this stage the known
tracked files that might possibly be deleted/modified locally, but not
listed remotely in the svndiff documents (because they wouldn't change),
will be restored using the _get-file_ command. It takes much longer, since
the local copy of the repository is fully traversed.  In many cases this
stage is not actually needed.  If it is known that the local files were
not modified/moved/removed since the last update, no extra check is
needed. This stage can be turned on using the _-R_ (repair mode) option.
Actually a fresh re-checkout into an empty directory will often be quicker
than this stage.

This program was first written as a proof of concept, mostly in order to
explore the design decision. It can, however, be used in real world tasks.
It can be built using basic [GNU autotools][4]. in this program I have
used another [my library of common functions][5] (just for my convenience).
This can be easily integrated into a single self-contained project. On
FreeBSD, it can be turned into a port, please see the port directory.

Everything is released under 2-clause BSD license.


TODO
====

* Support of different transports: http://, https:// svn+ssh://.
* Handle a previously interrupted run of update.
* Support of a lock file to prevent stepping on itself.
* Portability to other platforms (?).


[1]: http://svn.apache.org/repos/asf/subversion/trunk/subversion/libsvn_ra_svn/protocol "RA SVN Protocol Specification"
[2]: http://svn.apache.org/repos/asf/subversion/trunk/notes/svndiff
[3]: https://metacpan.org/module/Parse::SVNDiff
[4]: http://en.wikipedia.org/wiki/GNU_build_system 
[5]: https://github.com/mkushnir/mrkcommon
[6]: http://svn.apache.org/repos/asf/subversion/trunk/notes/http-and-webdav/webdav-usage.html
