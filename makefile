ifdef DEBUG
    export CP_DEBUG=yes
    export DP_DEBUG=yes
    export CLI_APP_DEBUG=yes
    PREFIX := /debug
else
    PREFIX := 
endif

# All Target
all: alvs

alvs: alvs_dp alvs_cp alvs_cli

alvs_dp: 
	# Prepare objects folder
	mkdir -p build/alvs/src/dp
	mkdir -p bin$(PREFIX)
	
	# call specific make 
	CONFIG_ALVS=1 $(MAKE) -f dp.mk make_dp

alvs_cli:
	# Prepare objects folder
	mkdir -p build/alvs/src/common
	mkdir -p build/alvs/src/cli
	mkdir -p bin$(PREFIX)
	
	# call specific make
	CONFIG_ALVS=1 $(MAKE) -f cli.mk make_cli

alvs_cp:
	# Prepare objects folder
	mkdir -p build/alvs/src/common
	mkdir -p build/alvs/src/cp
	mkdir -p bin$(PREFIX)
	
	# call specific make
	CONFIG_ALVS=1 $(MAKE) -f cp.mk make_cp


dp-clean:
	$(MAKE) -f dp.mk dp-clean

cp-clean:
	$(MAKE) -f cp.mk cp-clean
	
cli-clean:
	$(MAKE) -f cli.mk cli-clean

clean: dp-clean cp-clean cli-clean
	rm -rf bin	
	rm -rf build
