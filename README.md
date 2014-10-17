mboxstats: create top-10 lists from messages in mbox-format
===========================================================

See home page at http://www.vanheusden.com/mboxstats/.

mboxstats creates top-10 lists from an mbox-format archive, for
example:

    Top writes
    Top receivers
    Top subjects
    Top cc'ers
    Top top-level-domain
    Top timezones
    Top organisations
    Top useragents (mailprograms)
    Top month/day-of-month/day-of-week/hour
    Average number of lines per message
    All kinds of per-user statistics
    And much more! 

Since mboxstats doesn't have its own README, I made this one based on
http://www.vanheusden.com/mboxstats/.  As far as I can tell, the
author does not keep the mboxstats source code in public version
control repository.  The first Conservatory import was based on
version 3.0 from http://www.vanheusden.com/mboxstats/mboxstats-3.0.tgz
in Dec 2013.  Later, the changes for version 3.1 were brought in on 
17 Oct 2014 (see commit 964f42edd45 and commit e1f8931d41a), based on
http://www.vanheusden.com/mboxstats/mboxstats-3.1.tgz.

On the web site, the author asks people to report usage of mboxstats:
http://www.vanheusden.com/mboxstats/feedbackform.php?subject=mboxstats

Installation
------------

The usual:

    $ make
    $ make install

You'll get a lot of warnings on the `make` step; just ignore them.

Cute trick: if you want to let the author know you're using mboxstats,
you can apparently run

    $ make thanks

Usage
-----

After building, run

    $ ./mboxstats -h

to see usage instructions.

Feedback
--------

I suggest using the author's feedback from, rather than bug reports
attached to this repository, to send feedback about mboxstats:

  http://www.vanheusden.com/mboxstats/feedbackform.php?subject=mboxstats

-Karl Fogel (@kfogel)
 [not the author of mboxstats, merely the repository assembler]
