#! /bin/sh

TEMPDIR=/tmp/pspp-tst-$$
mkdir -p $TEMPDIR
trap 'cd /; rm -rf $TEMPDIR' 0

# ensure that top_builddir  are absolute
if [ -z "$top_builddir" ] ; then top_builddir=. ; fi
if [ -z "$top_srcdir" ] ; then top_srcdir=. ; fi
top_builddir=`cd $top_builddir; pwd`
PSPP=$top_builddir/src/ui/terminal/pspp

# ensure that top_srcdir is absolute
top_srcdir=`cd $top_srcdir; pwd`

STAT_CONFIG_PATH=$top_srcdir/config
export STAT_CONFIG_PATH

fail()
{
    echo $activity
    echo FAILED
    exit 1;
}


no_result()
{
    echo $activity
    echo NO RESULT;
    exit 2;
}

pass()
{
    exit 0;
}

cd $TEMPDIR

activity="write pspp syntax"
cat > wkday-out.pspp <<EOF
set errors=none.
set mxwarns=10000000.
set mxerrs=10000000.
data list /x 1-10.
begin data.

0
0.5
0.9
1
2
3
4
4.1
4.5
4.9
5
6
7
7.1
7.5
7.9
8
end data.
print outfile='wkday-out.out'/x(wkday2).
print outfile='wkday-out.out'/x(wkday3).
print outfile='wkday-out.out'/x(wkday4).
print outfile='wkday-out.out'/x(wkday5).
print outfile='wkday-out.out'/x(wkday6).
print outfile='wkday-out.out'/x(wkday7).
print outfile='wkday-out.out'/x(wkday8).
print outfile='wkday-out.out'/x(wkday9).
print outfile='wkday-out.out'/x(wkday10).
print outfile='wkday-out.out'/x(wkday11).
print outfile='wkday-out.out'/x(wkday12).
print outfile='wkday-out.out'/x(wkday13).
print outfile='wkday-out.out'/x(wkday14).
print outfile='wkday-out.out'/x(wkday15).
print outfile='wkday-out.out'/x(wkday16).
print outfile='wkday-out.out'/x(wkday17).
print outfile='wkday-out.out'/x(wkday18).
print outfile='wkday-out.out'/x(wkday19).
print outfile='wkday-out.out'/x(wkday20).
print outfile='wkday-out.out'/x(wkday21).
print outfile='wkday-out.out'/x(wkday22).
print outfile='wkday-out.out'/x(wkday23).
print outfile='wkday-out.out'/x(wkday24).
print outfile='wkday-out.out'/x(wkday25).
print outfile='wkday-out.out'/x(wkday26).
print outfile='wkday-out.out'/x(wkday27).
print outfile='wkday-out.out'/x(wkday28).
print outfile='wkday-out.out'/x(wkday29).
print outfile='wkday-out.out'/x(wkday30).
print outfile='wkday-out.out'/x(wkday31).
print outfile='wkday-out.out'/x(wkday32).
print outfile='wkday-out.out'/x(wkday33).
print outfile='wkday-out.out'/x(wkday34).
print outfile='wkday-out.out'/x(wkday35).
print outfile='wkday-out.out'/x(wkday36).
print outfile='wkday-out.out'/x(wkday37).
print outfile='wkday-out.out'/x(wkday38).
print outfile='wkday-out.out'/x(wkday39).
print outfile='wkday-out.out'/x(wkday40).
execute.
EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="run program"
$SUPERVISOR $PSPP --testing-mode wkday-out.pspp
if [ $? -eq 0 ] ; then no_result ; fi

activity="compare output"
diff -u wkday-out.out - <<EOF 
  .
   .
    .
     .
      .
       .
        .
         .
          .
           .
            .
             .
              .
               .
                .
                 .
                  .
                   .
                    .
                     .
                      .
                       .
                        .
                         .
                          .
                           .
                            .
                             .
                              .
                               .
                                .
                                 .
                                  .
                                   .
                                    .
                                     .
                                      .
                                       .
                                        .
  .
   .
    .
     .
      .
       .
        .
         .
          .
           .
            .
             .
              .
               .
                .
                 .
                  .
                   .
                    .
                     .
                      .
                       .
                        .
                         .
                          .
                           .
                            .
                             .
                              .
                               .
                                .
                                 .
                                  .
                                   .
                                    .
                                     .
                                      .
                                       .
                                        .
  .
   .
    .
     .
      .
       .
        .
         .
          .
           .
            .
             .
              .
               .
                .
                 .
                  .
                   .
                    .
                     .
                      .
                       .
                        .
                         .
                          .
                           .
                            .
                             .
                              .
                               .
                                .
                                 .
                                  .
                                   .
                                    .
                                     .
                                      .
                                       .
                                        .
  .
   .
    .
     .
      .
       .
        .
         .
          .
           .
            .
             .
              .
               .
                .
                 .
                  .
                   .
                    .
                     .
                      .
                       .
                        .
                         .
                          .
                           .
                            .
                             .
                              .
                               .
                                .
                                 .
                                  .
                                   .
                                    .
                                     .
                                      .
                                       .
                                        .
 SU
 SUN
 SUND
 SUNDA
 SUNDAY
 SUNDAY 
 SUNDAY  
 SUNDAY   
 SUNDAY    
 SUNDAY     
 SUNDAY      
 SUNDAY       
 SUNDAY        
 SUNDAY         
 SUNDAY          
 SUNDAY           
 SUNDAY            
 SUNDAY             
 SUNDAY              
 SUNDAY               
 SUNDAY                
 SUNDAY                 
 SUNDAY                  
 SUNDAY                   
 SUNDAY                    
 SUNDAY                     
 SUNDAY                      
 SUNDAY                       
 SUNDAY                        
 SUNDAY                         
 SUNDAY                          
 SUNDAY                           
 SUNDAY                            
 SUNDAY                             
 SUNDAY                              
 SUNDAY                               
 SUNDAY                                
 SUNDAY                                 
 SUNDAY                                  
 MO
 MON
 MOND
 MONDA
 MONDAY
 MONDAY 
 MONDAY  
 MONDAY   
 MONDAY    
 MONDAY     
 MONDAY      
 MONDAY       
 MONDAY        
 MONDAY         
 MONDAY          
 MONDAY           
 MONDAY            
 MONDAY             
 MONDAY              
 MONDAY               
 MONDAY                
 MONDAY                 
 MONDAY                  
 MONDAY                   
 MONDAY                    
 MONDAY                     
 MONDAY                      
 MONDAY                       
 MONDAY                        
 MONDAY                         
 MONDAY                          
 MONDAY                           
 MONDAY                            
 MONDAY                             
 MONDAY                              
 MONDAY                               
 MONDAY                                
 MONDAY                                 
 MONDAY                                  
 TU
 TUE
 TUES
 TUESD
 TUESDA
 TUESDAY
 TUESDAY 
 TUESDAY  
 TUESDAY   
 TUESDAY    
 TUESDAY     
 TUESDAY      
 TUESDAY       
 TUESDAY        
 TUESDAY         
 TUESDAY          
 TUESDAY           
 TUESDAY            
 TUESDAY             
 TUESDAY              
 TUESDAY               
 TUESDAY                
 TUESDAY                 
 TUESDAY                  
 TUESDAY                   
 TUESDAY                    
 TUESDAY                     
 TUESDAY                      
 TUESDAY                       
 TUESDAY                        
 TUESDAY                         
 TUESDAY                          
 TUESDAY                           
 TUESDAY                            
 TUESDAY                             
 TUESDAY                              
 TUESDAY                               
 TUESDAY                                
 TUESDAY                                 
 WE
 WED
 WEDN
 WEDNE
 WEDNES
 WEDNESD
 WEDNESDA
 WEDNESDAY
 WEDNESDAY 
 WEDNESDAY  
 WEDNESDAY   
 WEDNESDAY    
 WEDNESDAY     
 WEDNESDAY      
 WEDNESDAY       
 WEDNESDAY        
 WEDNESDAY         
 WEDNESDAY          
 WEDNESDAY           
 WEDNESDAY            
 WEDNESDAY             
 WEDNESDAY              
 WEDNESDAY               
 WEDNESDAY                
 WEDNESDAY                 
 WEDNESDAY                  
 WEDNESDAY                   
 WEDNESDAY                    
 WEDNESDAY                     
 WEDNESDAY                      
 WEDNESDAY                       
 WEDNESDAY                        
 WEDNESDAY                         
 WEDNESDAY                          
 WEDNESDAY                           
 WEDNESDAY                            
 WEDNESDAY                             
 WEDNESDAY                              
 WEDNESDAY                               
 WE
 WED
 WEDN
 WEDNE
 WEDNES
 WEDNESD
 WEDNESDA
 WEDNESDAY
 WEDNESDAY 
 WEDNESDAY  
 WEDNESDAY   
 WEDNESDAY    
 WEDNESDAY     
 WEDNESDAY      
 WEDNESDAY       
 WEDNESDAY        
 WEDNESDAY         
 WEDNESDAY          
 WEDNESDAY           
 WEDNESDAY            
 WEDNESDAY             
 WEDNESDAY              
 WEDNESDAY               
 WEDNESDAY                
 WEDNESDAY                 
 WEDNESDAY                  
 WEDNESDAY                   
 WEDNESDAY                    
 WEDNESDAY                     
 WEDNESDAY                      
 WEDNESDAY                       
 WEDNESDAY                        
 WEDNESDAY                         
 WEDNESDAY                          
 WEDNESDAY                           
 WEDNESDAY                            
 WEDNESDAY                             
 WEDNESDAY                              
 WEDNESDAY                               
 WE
 WED
 WEDN
 WEDNE
 WEDNES
 WEDNESD
 WEDNESDA
 WEDNESDAY
 WEDNESDAY 
 WEDNESDAY  
 WEDNESDAY   
 WEDNESDAY    
 WEDNESDAY     
 WEDNESDAY      
 WEDNESDAY       
 WEDNESDAY        
 WEDNESDAY         
 WEDNESDAY          
 WEDNESDAY           
 WEDNESDAY            
 WEDNESDAY             
 WEDNESDAY              
 WEDNESDAY               
 WEDNESDAY                
 WEDNESDAY                 
 WEDNESDAY                  
 WEDNESDAY                   
 WEDNESDAY                    
 WEDNESDAY                     
 WEDNESDAY                      
 WEDNESDAY                       
 WEDNESDAY                        
 WEDNESDAY                         
 WEDNESDAY                          
 WEDNESDAY                           
 WEDNESDAY                            
 WEDNESDAY                             
 WEDNESDAY                              
 WEDNESDAY                               
 WE
 WED
 WEDN
 WEDNE
 WEDNES
 WEDNESD
 WEDNESDA
 WEDNESDAY
 WEDNESDAY 
 WEDNESDAY  
 WEDNESDAY   
 WEDNESDAY    
 WEDNESDAY     
 WEDNESDAY      
 WEDNESDAY       
 WEDNESDAY        
 WEDNESDAY         
 WEDNESDAY          
 WEDNESDAY           
 WEDNESDAY            
 WEDNESDAY             
 WEDNESDAY              
 WEDNESDAY               
 WEDNESDAY                
 WEDNESDAY                 
 WEDNESDAY                  
 WEDNESDAY                   
 WEDNESDAY                    
 WEDNESDAY                     
 WEDNESDAY                      
 WEDNESDAY                       
 WEDNESDAY                        
 WEDNESDAY                         
 WEDNESDAY                          
 WEDNESDAY                           
 WEDNESDAY                            
 WEDNESDAY                             
 WEDNESDAY                              
 WEDNESDAY                               
 TH
 THU
 THUR
 THURS
 THURSD
 THURSDA
 THURSDAY
 THURSDAY 
 THURSDAY  
 THURSDAY   
 THURSDAY    
 THURSDAY     
 THURSDAY      
 THURSDAY       
 THURSDAY        
 THURSDAY         
 THURSDAY          
 THURSDAY           
 THURSDAY            
 THURSDAY             
 THURSDAY              
 THURSDAY               
 THURSDAY                
 THURSDAY                 
 THURSDAY                  
 THURSDAY                   
 THURSDAY                    
 THURSDAY                     
 THURSDAY                      
 THURSDAY                       
 THURSDAY                        
 THURSDAY                         
 THURSDAY                          
 THURSDAY                           
 THURSDAY                            
 THURSDAY                             
 THURSDAY                              
 THURSDAY                               
 THURSDAY                                
 FR
 FRI
 FRID
 FRIDA
 FRIDAY
 FRIDAY 
 FRIDAY  
 FRIDAY   
 FRIDAY    
 FRIDAY     
 FRIDAY      
 FRIDAY       
 FRIDAY        
 FRIDAY         
 FRIDAY          
 FRIDAY           
 FRIDAY            
 FRIDAY             
 FRIDAY              
 FRIDAY               
 FRIDAY                
 FRIDAY                 
 FRIDAY                  
 FRIDAY                   
 FRIDAY                    
 FRIDAY                     
 FRIDAY                      
 FRIDAY                       
 FRIDAY                        
 FRIDAY                         
 FRIDAY                          
 FRIDAY                           
 FRIDAY                            
 FRIDAY                             
 FRIDAY                              
 FRIDAY                               
 FRIDAY                                
 FRIDAY                                 
 FRIDAY                                  
 SA
 SAT
 SATU
 SATUR
 SATURD
 SATURDA
 SATURDAY
 SATURDAY 
 SATURDAY  
 SATURDAY   
 SATURDAY    
 SATURDAY     
 SATURDAY      
 SATURDAY       
 SATURDAY        
 SATURDAY         
 SATURDAY          
 SATURDAY           
 SATURDAY            
 SATURDAY             
 SATURDAY              
 SATURDAY               
 SATURDAY                
 SATURDAY                 
 SATURDAY                  
 SATURDAY                   
 SATURDAY                    
 SATURDAY                     
 SATURDAY                      
 SATURDAY                       
 SATURDAY                        
 SATURDAY                         
 SATURDAY                          
 SATURDAY                           
 SATURDAY                            
 SATURDAY                             
 SATURDAY                              
 SATURDAY                               
 SATURDAY                                
 SA
 SAT
 SATU
 SATUR
 SATURD
 SATURDA
 SATURDAY
 SATURDAY 
 SATURDAY  
 SATURDAY   
 SATURDAY    
 SATURDAY     
 SATURDAY      
 SATURDAY       
 SATURDAY        
 SATURDAY         
 SATURDAY          
 SATURDAY           
 SATURDAY            
 SATURDAY             
 SATURDAY              
 SATURDAY               
 SATURDAY                
 SATURDAY                 
 SATURDAY                  
 SATURDAY                   
 SATURDAY                    
 SATURDAY                     
 SATURDAY                      
 SATURDAY                       
 SATURDAY                        
 SATURDAY                         
 SATURDAY                          
 SATURDAY                           
 SATURDAY                            
 SATURDAY                             
 SATURDAY                              
 SATURDAY                               
 SATURDAY                                
 SA
 SAT
 SATU
 SATUR
 SATURD
 SATURDA
 SATURDAY
 SATURDAY 
 SATURDAY  
 SATURDAY   
 SATURDAY    
 SATURDAY     
 SATURDAY      
 SATURDAY       
 SATURDAY        
 SATURDAY         
 SATURDAY          
 SATURDAY           
 SATURDAY            
 SATURDAY             
 SATURDAY              
 SATURDAY               
 SATURDAY                
 SATURDAY                 
 SATURDAY                  
 SATURDAY                   
 SATURDAY                    
 SATURDAY                     
 SATURDAY                      
 SATURDAY                       
 SATURDAY                        
 SATURDAY                         
 SATURDAY                          
 SATURDAY                           
 SATURDAY                            
 SATURDAY                             
 SATURDAY                              
 SATURDAY                               
 SATURDAY                                
 SA
 SAT
 SATU
 SATUR
 SATURD
 SATURDA
 SATURDAY
 SATURDAY 
 SATURDAY  
 SATURDAY   
 SATURDAY    
 SATURDAY     
 SATURDAY      
 SATURDAY       
 SATURDAY        
 SATURDAY         
 SATURDAY          
 SATURDAY           
 SATURDAY            
 SATURDAY             
 SATURDAY              
 SATURDAY               
 SATURDAY                
 SATURDAY                 
 SATURDAY                  
 SATURDAY                   
 SATURDAY                    
 SATURDAY                     
 SATURDAY                      
 SATURDAY                       
 SATURDAY                        
 SATURDAY                         
 SATURDAY                          
 SATURDAY                           
 SATURDAY                            
 SATURDAY                             
 SATURDAY                              
 SATURDAY                               
 SATURDAY                                
  .
   .
    .
     .
      .
       .
        .
         .
          .
           .
            .
             .
              .
               .
                .
                 .
                  .
                   .
                    .
                     .
                      .
                       .
                        .
                         .
                          .
                           .
                            .
                             .
                              .
                               .
                                .
                                 .
                                  .
                                   .
                                    .
                                     .
                                      .
                                       .
                                        .
EOF
if [ $? -ne 0 ] ; then fail ; fi

pass
