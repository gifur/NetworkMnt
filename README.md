MonitorService
============================
__Run the driver as a service.__ this program will install the driver as a service.

__Usage:__

`MonitorService.exe 0` ; install and run the service

`MonitorService.exe 1` ; stop and unstall the service

MonitorApp
============================
console program which display the netowrk statistic of the progress which is having internet communication.
it would show the progress id and progress name and the upload & download flow per second, and the total flow it used since
the __MonitorApp__ has been started.

`Ctrl + C`：stop and quit the app.

`Ctrl + Break`：snapshot about how many flow data has been used.

NetworkMnt
============================
the driver implement code base on WDF and WFP

Refer to:  __Windows Filtering Platform MSN Messenger Monitor Sample__
