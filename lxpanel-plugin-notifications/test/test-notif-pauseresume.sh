#!/bin/bash
#
# Simple script to test notifications widget "pause" and "resume" verbs
# Execute this from your regular user account.
#

pipe_filename="$HOME/.kano-notifications.fifo"

if [ ! -p "$pipe_filename" ]; then
    echo "Widget pipe was not found: $pipe_filename"
    exit 1
fi

echo "pausing the widget notifications"
echo "pause" >> $pipe_filename
sleep 3

echo "queueing 3 notifications"
echo "badges:master:computer_commander" >> $pipe_filename
echo "badges:master:computer_commander" >> $pipe_filename
echo "badges:master:computer_commander" >> $pipe_filename

sleep 1
echo "resuming widget notifications - alerts should show up now"
echo "resume" >> $pipe_filename
exit 0
