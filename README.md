# ESPr Developer + Ambientで作る「ベランダ環境モニター」

ESP-WROOM-02開発ボード「ESPr Developer」とIoTクラウドサービス[Ambient](https://ambidata.io)を使い、
温度、湿度などを周期的に測定する環境モニターのソースコードです。
解説記事を[ESPr Developer + Ambientで自宅の環境モニターを作る](https://ambidata.io/examples/weatherstation/)に書きましたので、ご覧ください。

ここには2種類のソースコードがあります。

* examples/SimpleWeatherStation

温度、湿度、気圧センサーBME280と照度センサーNJL7502Lを使い、5分に1回、測定。30分に1回Wi-Fiに接続し、Ambientにデーターを送信する。

* examples/SimpleWeatherStationConfPortal

使用しているセンサー、測定方法はSimpleWeatherStationと同じ。Wi-FiのSSID、パスワード、AmbientのチャネルID、ライトキーをプログラムに書かず、スマホなど外部から設定できる。
