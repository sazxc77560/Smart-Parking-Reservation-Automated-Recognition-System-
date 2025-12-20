# 指定編譯目標為 alarm_driver.o
obj-m += alarm_driver.o

# 指向您目前正在運行的核心版本 Headers
KDIR = /lib/modules/$(shell uname -r)/build

all:
	# 呼叫核心 build 系統來編譯模組
	make -C $(KDIR) M=$(shell pwd) modules

clean:
	# 清除編譯產生的暫存檔
	make -C $(KDIR) M=$(shell pwd) clean
