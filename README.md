# RSSI_Mointor
共四个工程。

link_monitor_ctrl——中控，提供给客户的正式版本。

link_monitor_node_new——新的监控节点，可以同时监控主从，但是只能获得主从的RSSI，无法获得payload。有很多的调试log。

link_monitor_node_new-nolog——新的监控节点，相对`link_monitor_node_new`去掉了打印log，是可以提供给客户的正式版本

link_monitor_node——旧的监控节点，只能监控主，且未经过长期老化、测试，可能还存在一些问题。相对new版本节点，优势在于可以获得payload。如果客户有这个需求，需要再进行老化调试。



综上，我们现在提供给客户的正式版本是**中控**（link_monitor_ctrl）和**节点**（link_monitor_node_new-nolog）两个工程。



