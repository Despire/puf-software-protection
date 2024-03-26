take-measurements:
	cd ./scripts && ./enroll.sh

enroll:
	mkdir -p enroll_BBB
	./enroll/target/release/enroll ./bbb_config.json

clean:
	rm -r ./enroll/target
	rm enroll.json

.PHONY: all enroll take_measurements clean build
