# Perfect Pop

## Motivation 

Have you ever burnt popcorn? Or stopped the microwave too early and had a bunch of unpopped kernels?

Well, I have. 

One evening, while preparing popcorn for a movie night with my wife, I started counting the pops and noticed how the popping rate changed over time.

This gives us a histogram of the pops over time. 

// plot of a Bell curve 

Suddenly an idea came to mind: 
What if this characteristic is always the same?

You can change the microwave, the power, the brand of the popcorn, but the shape of this curve remains. 

// plot of the effect of different microwave power setting, and number of kernels in the bag

The higher the microwave power, the faster the kernels will pop. But if you leave everything else unchanged, the curve gets squeezed, but the shape remains the same. Also the area under the curve (i.e. the total number of kernels in the bag) will be the same. 

If this is true (to be proven...), and we count the pops during the making of the popcorn, then we can plot the histogram and fit a curve to the observed popping rate, and predict how many more kernels might pop. 

// plot of the Bell curve fit

With this the optimal heating time can be calculated without knowing the number of kernels, or even the power of the microwave. 

Could the changing popping rate help predict when to stop heating? My hypothesis is that different batches follow a sufficiently similar pattern to make this possible. The plan is to detect individual pops, estimate their rate over time, and test whether fitting a model can predict the remaining popping activity without knowing the batch size or microwave power in advance.

## Architecture 

I found an STM32 F769 Discovery board in my drawer. It seemed suitable, as it has microphones to capture the pops, and a screen to display the statistics, or maybe plot the histogram. Also in the last few months I only worked on POSIX systems (mainly QNX, embedded linux), so I kind of felt glad to drop back to bare metal. 

### HW 

STM32 F769 Disco board ... todo ... 

### System architecture 

// todo ... 


## Why this project?  

Is this complete over-engineering? 
Yes. 
Is this more engineering than movie night strictly requires? 
Yes. 
Am I learning something with this project? 
Yes. 
Am I having fun? 
Absolutely.