# xml_util
Utility functions to convert between protobuf binary format and proto3 XML format.

## Start
```console
foo@bar:~$ cd ~/codebase 
foo@bar:~$ git clone https://github.com/protocolbuffers/protobuf.git 
foo@bar:~$ cd .. 
foo@bar:~$ git clone https://github.com/williammuji/pb_xml_util.git
foo@bar:~$ cd .. 
foo@bar:~$ cp -R pb_xml_util/* protobuf/ 
foo@bar:~$ cd protobuf 
foo@bar:~$ bazel build :all 
foo@bar:~$ bazel-bin/add_person_cpp address_book 
foo@bar:~$ bazel-bin/list_people_cpp address_book 
```
