# Xebec SASI/MFM S1510A
## SASI Signal Definitions 
The following tables list and define the signals that appear on the 
SASI Bus lines between the host adapter and the controller. 
TABLE 4-1 HOST BUS STATUS SIGNALS 
DRV/RCVR DEFINITION 
Drv OC Input-/Output: The controller drives this 
line. A low level on this line indicates 
that the controller is driving the data in 
on the host bus. A high level on this line 
indicates that the host adapter is driving 
the data out on the host bus. The host 
adapter monitors this line and uses it to 
enable and disable its data bus drivers. 
This signal is qualified by signal REQ-. 

Drv OC Command-/Data: This signal line 
indicates whether the information on the 
data bus consists of command or data 
bytes. A low means command bytes; a 
high means data bytes. This signal is 
qualified by signal REQ-. 

Drv OC Busy: The controller generates this 
active low signal in response to the SEL signal and the address bit (DBO- to 
DB7-) from the host adapter. The busy 
signal informs the host adapter the 
controller is present and ready to 
conduct transactions on the host bus. 

Drv OC Message: The controller sends this 
active low signal to the host adapter to 
indicate that the current command has 
been completed. When MSG- is active, 
the 1-/О signal line is always low so that 
the controller can drive the bus data 
lines. This signal is qualified by signal 
REQ-. 


