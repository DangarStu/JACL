#!/bin/bash

find /usr/local/jacl/projects/temp/ -name '*.auto' -type f -mtime +14 | xargs rm
