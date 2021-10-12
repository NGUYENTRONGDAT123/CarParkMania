/* 
OVERVIEW
    Monitor Status of the LPR Sensors and keep track of where each car is in the car park

    Tell the Boom gates when to open and when to close (the boom gates are a simple piece 
    of hardware that can be only told to open or close, so the job of automatically closing
    the boom gates after they have been open for a little while is up to the manager)

    Control what is displayed on the information signs at each entrance

    As the manager knows where each car is, it is the manager's job to ensure that there is 
    room in the car park before allowing new vehicles in 
        (number of cars < number of levels * num cars per level)
    The manager also needs to keep track of how full the individual levels are and direct 
    new cars to a level that is not fully occupied 

    Keep track of how long each car has been in the parking lot and produce a bill once the
    car leaves

    Display the current status of the boom gates, signs, temperature sensors and alarms, 
    as well as how much revenie the car park has brought in so far

TIMINGS
    After the boom gate has been fully opened, it will start to close 20ms later. Cars 
    entering the car park will just drive in if the boom gate is fully open after they have
    been directed to a level (however, if the car arrives just as the boom gate starts to 
    close, it will have to wait for the boom gate to fully close, then fully open again.)

    Cars are billed based on how long they spend in the car park (see billing info)

VEHICLE AUTH
    Whenever a vehicle triggers an LPR, its plate should be checked against the contents of
    the plates.txt file. For performance/scalability reasons, the plates need to be read 
    into a hash table, which will then be checked when new vehicles show up. Using the 
    hash table exercise from Prac 3 as a base is recommended, although not required

    If a vehicles plate does not match with one in the plates.txt file, the digital sign
    will display the character 'X' and the boom gate will not open for that vehicle

BILLING
    Cars are billed at a rate of 5 cents for every millisecond they spend in the car park 
    (that is the total amount of time between the car showing up at the entrance LPR and
    the exit LPR). Cars who are turned away are not billed.

    This bill is tracked per car and the amount of time. It is shown in dollars and cents, 
    written next to the cars license plate:
        029MZH $8.25
        088FSB $20.80
    
    The manager writes these, line at a atime, to a file named billing.txt, each time a car 
    leaves the car park. 

    The billing.txt file will be created by the manager if it does not already exist, and must
    be opened in APPEND mode, which means that future lines will be written to the end of the
    file, if the file already exists (this will avoid the accidental overwriting of old records)
*/