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

```cpp
<root>
 <_list_people>
  <people name="sarah" id="1302" email="sarah@gmail.com">
   <_list_phones>
    <phones number="18601012332"></phones>
   </_list_phones>
  </people>
  <people name="will" id="10463" email="will@gmail.com">
   <_list_phones>
    <phones number="18917012112"></phones>
   </_list_phones>
  </people>
  <people name="nono" id="58" email="nono@gmail.com"></people>
  <people id="1001"></people>
 </_list_people>
</root>

people {
  name: "sarah"
  id: 1302
  email: "sarah@gmail.com"
  phones {
    number: "18601012332"
  }
}
people {
  name: "will"
  id: 10463
  email: "will@gmail.com"
  phones {
    number: "18917012112"
  }
}
people {
  name: "nono"
  id: 58
  email: "nono@gmail.com"
}
people {
  id: 1001
}
```
