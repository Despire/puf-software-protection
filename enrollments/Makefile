take-measurements:
	cd ./scripts && ./enroll.sh

enroll:
	@if [ ! -d "enroll_BBB" ]; then \
		unzip "enroll_bbb.zip"; \
	fi
	./enroll/target/release/enroll ./bbb_config.json

clean:
	rm -r ./enroll/target
	rm enroll.json

.PHONY: all enroll take_measurements clean build
