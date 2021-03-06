PULSE - One-shot pulse delay and stretch
========================================

A PULSE block produces configurable width output pulses with an optional delay
based on its parameters. If WIDTH is non-zero, the output pulse width will be
the specified amount. If DELAY is non-zero, the pulse train will be delayed
by that amount. If both are non-zero, the pulses are stretched and delayed as
long as the resulting output would still contain the same number of distinct
pulses. If this is not the case, then the PERR signal is raised, and the
MISSED_CNT counter is incremented. Change of any parameter causes the block to
be reset.

Fields
------

.. block_fields:: modules/pulse/pulse.block.ini

Zero Delay
----------

If DELAY=0, then the INP pulse will be stretched with only the propagation delay
of the block (1 clock tick). WIDTH must be at least 4, and any value given below
is defaulted to four.

.. timing_plot::
   :path: modules/pulse/pulse.timing.ini
   :section: Pulse stretching with no delay activate on rising edge

.. timing_plot::
   :path: modules/pulse/pulse.timing.ini
   :section: No delay means a WIDTH >3 is required

Zero Width
----------

If WIDTH=0, then the INP pulse width will be used. DELAY must be >3 clock ticks,
any lower inputted values will be defaulted to four.

.. timing_plot::
   :path: modules/pulse/pulse.timing.ini
   :section: Pulse delay with no stretch

.. timing_plot::
   :path: modules/pulse/pulse.timing.ini
   :section: No WIDTH means a delay >3 is required

Width and Delay
---------------

In this mode, pulses are placed onto an output queue, so a number of
restrictions apply:

* There must not be more than 1023 pulses on the output queue
* WIDTH must be >3 clock ticks
* There must be >3 clock ticks where output is 0 between pulses. This means
  that WIDTH < T - 3 where T is the minimum INP pulse period

.. timing_plot::
   :path: modules/pulse/pulse.timing.ini
   :section: Pulse delay and stretch

.. timing_plot::
   :path: modules/pulse/pulse.timing.ini
   :section: Pulse train stretched and delayed

.. timing_plot::
   :path: modules/pulse/pulse.timing.ini
   :section: No delay or stretch

Different Edge Activation
-------------------------

When there is a width specified, it is possible to also specify which edge of
the input pulse activates the output.

.. timing_plot::
   :path: modules/pulse/pulse.timing.ini
   :section: Pulse stretching with no delay activate on falling edge

.. timing_plot::
   :path: modules/pulse/pulse.timing.ini
   :section: Pulse stretching with no delay activate on both edges

Pulse period error
------------------

The following example shows what happens when the period between pulses is too
short.

.. timing_plot::
   :path: modules/pulse/pulse.timing.ini
   :section: Stretched and delayed pulses too close together

