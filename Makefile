CPP = g++
CPPFLAGS += -std=c++11 -pthread -g -Dcimg_display=0
OPTFLAGS += -Wall
INCLUDES = -I./fastflow/

OBJECTS = out-st \
		  out-tp \
		  out-ff

OUT = img/result
OUTFF = img/result_ff


default: $(OBJECTS)

## Compile project-singlethread.cpp
out-st: project-singlethread.cpp
	$(CPP) $(CPPFLAGS) $(OPTFLAGS) $< -o $@

## Compile project-threadpool.cpp
out-tp: project-threadpool.cpp
	$(CPP) $(CPPFLAGS) $(OPTFLAGS) $< -o $@

## Compile project-fastflow.cpp
out-ff: project-fastflow.cpp
	$(CPP) $(CPPFLAGS) $(INCLUDES) $< -o $@

## Remove all the objects created with make command
clean:
	rm $(OBJECTS)

## Remove all the objects created with make command and the directories with results
cleanall:
	rm $(OBJECTS)
	rm -r $(OUT)
	rm -r $(OUTFF)

## Run test-st test-tp test-ff
test:
	./out-st img/ watermark.jpg
	@echo "\n"
	./out-tp img/ watermark.jpg 0 16
	@echo "\n"
	./out-ff img/ watermark.jpg 0 16

## Run test for Singlethread approach
test-st:
	./out-st img/ watermark.jpg

## Run test for ThreadPool approach with parallel degree 16 and strategy 0
test-tp:
	./out-tp img/ watermark.jpg 0 16

## Run test for FastFlow approach with parallel degree 16 and strategy 0
test-ff:
	./out-ff img/ watermark.jpg 0 16

# COLORS
GREEN  := $(shell tput -Txterm setaf 2)
YELLOW := $(shell tput -Txterm setaf 3)
WHITE  := $(shell tput -Txterm setaf 7)
RESET  := $(shell tput -Txterm sgr0)
TARGET_MAX_CHAR_NUM=20
## Show help
help:
	@echo ''
	@echo 'Usage:'
	@echo '  ${YELLOW}make${RESET} ${GREEN}<target>${RESET}'
	@echo ''
	@echo 'Targets:'
	@awk '/^[a-zA-Z\-\_0-9]+:/ { \
		helpMessage = match(lastLine, /^## (.*)/); \
		if (helpMessage) { \
			helpCommand = substr($$1, 0, index($$1, ":")-1); \
			helpMessage = substr(lastLine, RSTART + 3, RLENGTH); \
			printf "  ${YELLOW}%-$(TARGET_MAX_CHAR_NUM)s${RESET} ${GREEN}%s${RESET}\n", helpCommand, helpMessage; \
		} \
	} \
	{ lastLine = $$0 }' $(MAKEFILE_LIST)
