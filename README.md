mrksvnup - A Fast _svnup_ For FreeBSD.
======================================


This program was inspired by pioneering work of John Mehr: [the
svnup utility](http://jcm.dsl.visi.com/freebsd/svnup/), and an earlier
Dag-Erling Smørgrav's experiment:
[svnsup](http://svnweb.freebsd.org/base/user/des/svnsup/).
My intention was to resolve some John's utility's problems of resource
usage. I also wanted to achieve a faster run time in at least the obvious
cases of small incremental updates.  The John's utility is based on the _main
command set_ of the SVN RA protocol. This approach, while yields a correct
net result, is probably not the most optimal in terms of performance. It
requires walking through the entire tree regardless of whether it's
actually needed or not, on the local side. It doesn't make benefit from
on-the-wire compression normally offered by SVN.

The idea was to inspect how the official
[svn](http://subversion.apache.org/) client completes some simple usage
scenarios: checking out a fresh tree, updating between _source_ and
_target_ revisions. The solution I think  was in the direction of des'
source code.

_mrksvnup_ includes a simple (and limited) implementation of the [SVN RA
protocol][1], and the [svndiff1][2] editor.

This program was first written as a proof of concept, mostly in order to
explore design solutions. It is, however, in its basic usable form.

The program supports the _svn://_ protocol only. Since this is my
homework, I made no provision for distributing it through ports, or other
packaging systems. It can be built using basic [GNU autotools][4].



[1]: http://svn.apache.org/repos/asf/subversion/trunk/subversion/libsvn_ra_svn/protocol "RA SVN Protocol Specification"
[2]: http://svn.apache.org/repos/asf/subversion/trunk/notes/svndiff
[3]: https://metacpan.org/module/Parse::SVNDiff
[4]: http://en.wikipedia.org/wiki/GNU_build_system 
