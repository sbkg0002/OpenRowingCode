//-------------------------------------------------------------------
//               clicks
unsigned long clicks = 0;                   // number of clicks since start
unsigned long clicksInDistance = 0;         // number of clicks already accounted for in the distance.
unsigned int diffclicks;                             // clicks from last stroke to this one
unsigned long driveStartclicks;             // number of clicks at start of drive.
byte currentrot = 0;                       // current rotation (for number of clicks per calculation)
//-------------------------------------------------------------------
//               global constants
float c = 2.8;                              //The figure used for c is somewhat arbitrary - selected to indicate a 'realistic' boat speed for a given output power. c/p = (v)^3 where p = power in watts, v = velocity in m/s  so v = (c/p)^1/3 v= (2.8/p)^1/3
                                            //Concept used to quote a figure c=2.8, which, for a 2:00 per 500m split (equivalent to u=500/120=4.17m/s) gives 203 Watts. 
//-------------------------------------------------------------------
//               rpm/angular Velocity
float previousDriveAngularVelocity;         // fastest angular velocity at end of previous drive
float driveAngularVelocity;                 // fastest angular velocity at end of drive
float recoveryAngularVelocity;              // angular velocity at the end of the recovery  (before drive)

//-------------------------------------------------------------------
//               acceleration/deceleration
const unsigned int consecutivedecelerations = 3;//number of consecutive decelerations before we are decelerating
const unsigned int consecutiveaccelerations = 3;// number of consecutive accelerations before detecting that we are accelerating.
unsigned int decelerations = consecutivedecelerations +1;             // number of decelerations detected.
unsigned int accelerations = 0;             // number of acceleration rotations;
//-------------------------------------------------------------------
//               drag factor 
int k3 = 0, k2 = 0, k1 = 0;                 // previous k values for smoothing                           
float mPerClick = 0;                        // meters per rotation of the flywheel (calculated from drag factor)

void setBoatType(short BoatType)
{
  boatType = BoatType;
  switch(BoatType)
  {
    case BOAT4:
      c = 2.8;
    break;
    case BOAT8:
      c = 2.8;
    break;
    case BOAT1:
      c = 2.8;
    break;
  }
}

void setErgType(short newErgType)
{
  ergType = newErgType;
  switch(newErgType)
  {
    case ERGTYPEVFIT:
        //V-Fit rower with tach.
        AnalogSwitch = false;
        I = 0.032;
        clicksPerRotation = 1;
        numclickspercalc = 1;
        k = 0.000085;  
        mStrokePerRotation = 0;//meters of stroke per rotation of the flywheel - V-fit.
        break;
    default:
        //Concept 2
        AnalogSwitch = true;
        I = 0.101;
        //do the calculations less often to allow inaccuracies to be averaged out.
        numclickspercalc = 1;//take out a lot of noise before we detect drive / recovery.
        //number of clicks per rotation is 3 as there are three magnets.
        clicksPerRotation = 3;
        k = 0.000125;
        mStrokePerRotation = 0.08;//meters of stroke per rotation of the flywheel - C2.
        ergType = ERGTYPEC2;    
        break;
  }
  mPerClick = pow((k/c),(0.33333333333333333))*2*PI/clicksPerRotation;
}

void registerClick()
{
  currentrot ++;
      clicks++;
      if(currentrot >= numclickspercalc)
      {
        currentrot = 0;
        //initialise the start time
        if(startTimems == 0) 
        {
          startTimems = mtime;
           #ifdef UseLCD
              lcd.clear();
           #endif
        }
        timetakenus = utime - laststatechangeus;
        rpmhistory[nextrpm] = (60000000.0*numclickspercalc/clicksPerRotation)/timetakenus;
        nextrpm ++;
        const int rpmarraycount = 3;
        int rpms[rpmarraycount] = {getRpm(0), getRpm(-1), getRpm(-2)};//,getRpm(-3),getRpm(-4)};
        if(nextrpm >=numRpms) nextrpm = 0;//wrap around to the start again.
        int currentmedianrpm = median(rpms,rpmarraycount);
        int rpms2[rpmarraycount] = {getRpm(-3), getRpm(-4),getRpm(-5)};//,getRpm(-8),getRpm(-9)};
        int previousmedianrpm = median(rpms2, rpmarraycount);
        if(currentmedianrpm > peakrpm) peakrpm = currentmedianrpm;
        float radSec = (float)currentmedianrpm/60*2*PI;
        float prevradSec = (float)previousmedianrpm/60*2*PI;
        float angulardeceleration = (prevradSec-radSec)/((float)timetakenus/1000000.0);
        //Serial.println(nextinstantaneousrpm);
        if(radSec > prevradSec || (accelerations > consecutiveaccelerations && radSec == prevradSec))//faster, or previously going faster and the same rpm
          { //lcd.print("Acc");        
            //on first acceleration - work out the total time decelerating.
            if(accelerations == 0 && decelerations > consecutivedecelerations) 
              {//first acceleration - capture the seconds decelerating and #
                //time for a single rotation at drive (which we will have included in secondsdecel but shouldn't have.
                secondsdecel = (float)((float)mtime- driveEndms)/1000;
                for(int i =0;i<consecutivedecelerations; i++)
                {//work back to get recovery time before our consecutive check.
                  secondsdecel -=(float)60/getRpm(0-i);
                }
              }
            accelerations ++;
            if(accelerations == consecutiveaccelerations && decelerations > consecutivedecelerations)
              {//beginning of drive /end recovery - we have been consistently decelerating and are now consistently accelerating
                totalStroke++;
                //work back to get recovery velocity and time before our consecutive check.
                recoveryAngularVelocity=(float)getRpm(-consecutivedecelerations-1)/60*2*PI;
                recoveryEndms = mtime;
                for(int i =0;i<consecutivedecelerations; i++)
                {//work back to get recovery time before our consecutive check.
                  recoveryEndms -=(float)60000/getRpm(0-i);
                }
                writeStrokeRow();
//                      if(totalStroke >9)
//                      {
//                        dumprpms();
//                      }
#ifdef debug
                Serial.println("\n");
                Serial.print("Total strokes:");
                Serial.print(totalStroke);
                Serial.print("\tSecondsDecelerating:\t");
                Serial.println(float(secondsdecel));
#endif
                //the number of seconds to add to deceleration which we missed as we were waiting for consecutive accelerations before we detected it.
                float nextk = I * ((1.0/recoveryAngularVelocity)-(1.0/driveAngularVelocity))/(secondsdecel)*1000000;
                driveAngularVelocity = radSec;
                if(nextk > 0 && nextk < 300)
                {//if drag factor detected is positive and reasonable
                  if(k3 ==0) 
                  {//reset all ks
                    k3 = nextk;
                    k2 = nextk;
                    k1 = nextk;
                  }
                  k3 = k2; 
                  k2 = k1;
                  k1 = nextk;  //nm/s/s == W/s/s
                  int karr[3] = {k1,k2,k3};
                  k = (float)median(karr,3)/1000000;  //adjust k by half of the difference from the last k
       #ifdef debug
                  Serial.print("k:"); Serial.println(nextk);
                  Serial.print("recw:"); Serial.println(recoveryAngularVelocity);
                  Serial.print("dw"); Serial.println(driveAngularVelocity);
                  Serial.print("sdecel"); Serial.println(secondsdecel);
       #endif
                  mPerClick = pow((k/c),(0.33333333333333333))*2*PI/clicksPerRotation;//v= (2.8/p)^1/3  
                }
                else
                {
            #ifdef debug
                  Serial.print("k:");
                  Serial.print(nextk);
                  Serial.print("recoveryrad");
                  Serial.print(recoveryAngularVelocity);
                  Serial.print("driverad");
                  Serial.print(driveAngularVelocity);
                  Serial.print("recoverySeconds");
                  Serial.print(secondsdecel);
            #endif
                }
                decelerations = 0;
                driveStartclicks = clicks;
                                //recovery is the stroke minus the drive, drive is just drive
                RecoveryToDriveRatio = (float)(secondsdecel) / ((float)lastDriveTimems/1000);
            #ifdef debug
                Serial.println();
                Serial.println(secondsdecel);
                Serial.println(((float)strokems/1000)-secondsdecel);
                Serial.println(RecoveryToDriveRatio);
            #endif
                //safest time to write a screen or to serial (slowest rpm)
              }
              else if(accelerations > consecutiveaccelerations)
              {
                //get the angular velocity before the change. 
                //set the drive angular velocity to be the value it was 4 clicks ago (before any deceleration
                driveAngularVelocity = (float)getRpm(-consecutiveaccelerations-1)/60*2*PI;
                driveEndms = mtime;
                for(int i =0;i<consecutiveaccelerations; i++)
                {//work back to get recovery time before our consecutive check.
                  driveEndms -=(float)60000/getRpm(0-i);
                }
                lastDriveTimems = driveEndms - recoveryEndms;
                //driveAngularVelocity = radSec;//and start monitoring the next drive (make the drive angular velocity low,
                decelerations = 0;
              }
          }
          else
          {
              decelerations ++;
              if(decelerations == consecutivedecelerations)
              {//still decelerating (more than three decelerations in a row).
                previousDriveAngularVelocity = driveAngularVelocity;    //store the previous deceleration
                diffclicks = clicks - laststrokeclicks;
                strokems = mtime - laststroketimems;
                spm = 60000 /strokems;
                laststrokeclicks = clicks;
                laststroketimems = mtime;
                split =  ((float)strokems)/((float)diffclicks*mPerClick*2) ;//time for stroke /1000 for ms *500 for 500m = /(*2)
                power = 2.8 / pow((split / 500),3.0);//watts = 2.8/(split/500)³ (from concept2 site)
                //Serial.print(split);
                //store the drive speed
                accelerations = 0;//reset accelerations counter
              }
              else if(decelerations > consecutivedecelerations)
              {
                driveLengthm = (float)(clicks - driveStartclicks) * mStrokePerRotation;
                accelerations = 0;//reset accelerations counter
              }
          }
          
          if(mPerClick <= 20)
          {
            distancem += (clicks-clicksInDistance)*mPerClick;
            clicksInDistance = clicks;
          }
          //if we are spinning slower than 10ms per spin then write the next screen
//          if(timetakenus > 10000) writeNextScreen();
          lastrotationus = timetakenus;
          //watch out for integer math problems here
          //Serial.println((nextinstantaneousrpm - instantaneousrpm)/timetakenms); 

              if((mtime-startTimems)/1000 > targetSeconds)
    {
      switch(sessionType)
      {
        case INTERVAL:
          if(intervals <= numIntervals)
          {
            showInterval(intervalSeconds); 
            //then reset the start time for count down to now for the next interval.
            startTimems = millis();
          }
          else
          {//stop.
            Serial.println("Done");
            while(true);
          }
          
          intervals ++;
          break;
        case TIME:
          Serial.println("Done");
          while(true);
          break;
        default:
        //default = = do nothing.
        break;
      }
    }
  }
}

String getTime()
{
  int timemins, timeseconds;
  String timeString = "";
        if(sessionType == TIME)
        {//count-down from the target time.
          timemins = (targetSeconds - (mtime-startTimems)/1000)/60;
          if(timemins < 0) timemins = 0;
          timeseconds = (targetSeconds - (mtime-startTimems)/1000) - timemins * 60 ;
          if(timeseconds < 0) timeseconds = 0;
        }
        else
        {
          timemins = (mtime-startTimems)/60000;
          timeseconds = (long)((mtime)-startTimems)/1000 - timemins*60;
        }
          if(timemins <10) timeString += "0";
          timeString += timemins;//total mins
          timeString += ":";
          if(timeseconds < 10) timeString +="0";
          timeString += timeseconds;//total seconds.*/
          return timeString;
}

//take time and display how long remains on the screen.
void showInterval(long numSeconds)
{
  Serial.print("Interval ");
  Serial.println(intervals);
  long startTime = millis()/1000;
  long currentTime = millis()/1000;
  while(startTime + numSeconds > currentTime)
  {
    delay(200);
    currentTime = millis()/1000;
    #ifdef UseLCD
      writeTimeLeft(startTime+numSeconds-currentTime);
    #endif
  }
  Serial.println("Interval Over");
}

String getSplitString()
{
  String splitString = "";
  int splitmin = (int)(split/60);
  int splits = (int)(((split-splitmin*60)));
  int splittenths = (int)((((split-splitmin*60)-splits))*10);
  splitString += splitmin;
  splitString += ":";
  if(splits <10) splitString += "0";
  splitString += splits;
  splitString +=".";
  splitString +=splittenths;
  return splitString;
}


void dumprpms()
{
    Serial.println("Rpm dump");
    for(int i = 0; i < numRpms; i++)
    {
      Serial.println(rpmhistory[i]);
    }
    //nextrpm = 0;
}




int median(int new_array[], int num){
     //ARRANGE VALUES
    for(int x=0; x<num; x++){
         for(int y=0; y<num-1; y++){
             if(new_array[y]>new_array[y+1]){
                 int temp = new_array[y+1];
                 new_array[y+1] = new_array[y];
                 new_array[y] = temp;
             }
         }
     }
    //CALCULATE THE MEDIAN (middle number)
    if(num % 2 != 0){// is the # of elements odd?
        int temp = ((num+1)/2)-1;
        //cout << "The median is " << new_array[temp] << endl;
  return new_array[temp];
    }
    else{// then it's even! :)
        //cout << "The median is "<< new_array[(num/2)-1] << " and " << new_array[num/2] << endl;
  return ((new_array[(num/2)-1] + new_array[num/2]) / 2); 
    }
}

int getRpm(short offset)
{
  if(offset >0) 
  {
    Serial.println("Warning, rpm in the future requested.");
  }
  int index = nextrpm - 1 + offset;
    while (index >= numRpms)
    {
      index -= numRpms;
    }
    while(index < 0) 
    {
      index += numRpms;
    }
  return rpmhistory[index];
}