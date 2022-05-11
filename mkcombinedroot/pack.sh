#!/bin/bash
echo -e "\033[32mStarting make boot.img and vendor.img!\033[0m"
source ./mkvendor_boot.sh
echo -e "\033[32mMake boot.img Done!\033[0m"
source ./mkvendor.sh
echo -e "\033[32mMake vendor.img Done!\033[0m"
