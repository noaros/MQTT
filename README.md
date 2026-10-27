# MQTT

I plan to make a simple demo on my STM32 Nucleo F429ZI that uses MQTT to stream the current internal temperature reading via the protocol to a dashboard. This is similar to [Kalman-Filter](https://github.com/noaros/Kalman-Filter), except replacing the filter with MQTT transmission. Also, I'll approach the project very differently.

The challenge in a post Claude world is how to still learn things efficiently while taking advantage of its magic. For some previous projects I've let Claude do a lot of work up front and then engaged in various means (including using Claude more!) to try and understand after the fact what it did. While I learned things, that approach felt unsatisfying. So for this one I'll try something different. I'll take more direct charge over the requirements, and proceed in small iterations, much as I did in the pre Claude days. I'll get feedback more often, and take time to understand and perhaps even refactor along the way. The most efficient learning technique so far has been simply interactively asking Claude questions. I'm hoping this new approach combines the interactive effectiveness of Q&A, but with the actual doing of building software, yet retaining a Claude speedup over the old days. 

## 0: UART printf starting point

This is just a typical starting point, a board with flashed firmware that periodically prints something to a serial port. I'll put it in branch 0. 
I ran 'make' then 'make flash' then saw the 'hello' output after running 'picocom -b 115200 /dev/ttyACM0'

## 1: Monitor

I switched to a shell command 'stty' to monitor the serial port, and put that into the makefile.

## 2: Shell

As an excuse to play, and because I like simple, I converted the makefile to shell scripts. Most wouldn't like this, I know. But the point of 'make' is to avoid building everything at once, which totally doesn't apply here. The new commands work.

## 3: Braces

More playing with Claude, I had it move the opening braces of functions to the same lines, lol.

## 4: Static

Removed 'static' keyword for less visual clutter. It really does look so much cleaner, but the trade-off is slightly larger file size, worse optimization, and global conflict. Most would probably want the keyword, so I really should just get used to it. Maybe in the next project. It is fun think about the tradeoffs. And amazing how one refactoring vs another, even an unconventional one like this, is a simple Claude request away.
