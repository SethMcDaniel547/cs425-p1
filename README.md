# Project 01

- Name: Seth McDaniel
- Email: sethmcdaniel547@u.boisestate.edu
- Class: CS425-001

## Experience

The hardest part was conceptualizing what needed to be done and how SMTP worked. Getting the first 2 layers done
at least made sense, but had no idea how to really do layer 3. The tests were pretty reasonable at first... but
at some point I just started having a few percent missing that I couldnt figure out how to get test coverage for and
just fed it into an AI to have it make tests. So there has to be a fair amount of overlap in those tests because that
file became so long as it would make a test which would cover 1-3% more... so I had it make a "few" tests to say the least.


## Design

### layer1 - Protocol helpers

String formatting, command construction, and parsing status codes

### layer2 - Session and reader

Sets it up to SMTP, validates multi-line replies and sets up the full transmission 

### layer3 - Socket

This is the part that actually sends what's been packaged in layer2



Splitting the code up into distinct layers helps in a few ways. I found that it made my test coverage way easier to reach since it
was so split up I could "reach" inside a bit better than I could before. Secondly if I wanted to update my socket section, I could 
reuse layer 1 and 2 still. 
