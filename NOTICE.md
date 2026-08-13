Shaduwulf's LinUwUx Runtime
===========================

Shaduwulf's LinUwUx Runtime is a standalone rework of the
functionality introduced by the original LinUwUx.patch.

The project moves the relevant LinUwUx behavior out of patched Wine/Proton
source trees and into a preloadable runtime library.

Origin and attribution
----------------------

The original LinUwUx.patch and the LinUwUx behavior implemented by it are
the work of LinUwUx.

This project adapts that behavior into a standalone runtime rather than
maintaining the same modifications directly inside Wine and Proton.

The runtime substantially restructures and extends the original approach
to operate as a preloadable shared library outside the Wine/Proton source tree.

Wine and Proton
---------------

LinUwUx.patch modifies Wine and Proton source code. Those upstream projects
retain their respective copyright notices and licenses.

Wine is licensed under the GNU Lesser General Public License version 2.1 or
later. Valve's Proton contains components under their respective licenses.

Shaduwulf's LinUwUx Runtime does not contain or distribute a patched Wine or
Proton source tree.

Project relationship
--------------------

Shaduwulf's LinUwUx Runtime is maintained as a standalone rework of
the behavior provided by LinUwUx.patch.

It is part of the Shaduwulf's Proton LinUwUx project family, but is designed
to operate independently of a custom Proton build.
