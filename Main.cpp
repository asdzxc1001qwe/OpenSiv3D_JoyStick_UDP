#include<Siv3D.hpp>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <Windows.h>

#pragma comment(lib, "ws2_32.lib")

//ラズベリーパイのIPアドレス
#define RaspberryPi_IP_ADDRESS "192.168.0.120"

#define C_x 400		//コントローラを描画するx座標.
#define C_y 300		//コントローラを描画するy座標.
#define SIZE  200	//コントローラの大きさ.

//	    	    	   ___			 ___	
//         _____	  |	  |			|   |	   _____			
//	      |  N  |	  |	M |			| S |	  |  T  |	
//		
//	    	 _____
//         d |   | b						 ○:X
//	      ___| c |___	  ○:V	   ○:W		
//       |		     |					
//       | e       a |				    ○:Y	  ○:A	
//       |___     ___|					
//	         | g |						
//         f |___| h						 ○:B
//
//											
//					 ／￣￣￣＼		 ／￣￣￣＼
//				    |          |	|          |	
//					|          |	|          |	
//				     ＼______／		 ＼______／
//
//

class JoyStick
{
private:
	//送信文字表示用のフォント.
	const Font font{ FontMethod::MSDF, 60, Typeface::Bold };

	// ソケット通信に関する変数.
	SOCKET sock;	//ソケット通信のための変数.
	uint16 PORT;	//ポート番号.
	sockaddr_in serverAddr;	//サーバーのアドレス情報を保持する構造体.
	char ip[16] = RaspberryPi_IP_ADDRESS;//ラズパイのIPアドレス.
	char send_buf[3];//送信データを格納するバッファ.

	const double send_UDP_interval = 0.1;//send_UDP関数の呼び出し間隔.
	double send_UDP_accumlatedTime = 0.0;//send_UDP関数の累積時間.

	// プレイヤーインデックス0の XInput コントローラを取得.
	s3d::detail::XInput_impl controller = XInput(0);

	//コントローラを表示する中心座標.
	double Center_x;	//x座標.
	double Center_y;	//y座標.

	//コントローラの大きさ.
	double r_Hex;	//六角形の半径.

	//4つの大円に関する情報.
	double Dist_Cir_Big;	//大円の中心と六角形の中心との距離.
	double r_Cir_Big;	//大円の半径.
	Array<Vec2> Arr_Loc_Cir_Big;	//大円の位置を含んだ配列.
	Array<Circle> Arr_Cir_Big;		//大円を格納する配列.

	//Back, Startボタンの情報.
	double Dist_Cir_Small;	//小円と中心との距離.
	double r_Cir_Small;	//小円の半径.
	Circle Cir_Small_L;	//小円左側準備.
	Circle Cir_Small_R;	//小円右側準備.

	//ボタンの大きさや座標などの情報.
	double r_Button;//A, B, Y, Xボタンの半径.
	double Dist_Button;//A, B, Y, Xボタンの大円中心からの距離.
	Array<Circle> Arr_ABXY;//A, B, X, Yボタンを格納する配列.

	//十字キーの準備.
	s3d::Polygon CrossKey;//十字キー概形.
	Array<Circle> Arr_Cir_POV;//十字キーの状態を表す8つの円が格納される配列.

	//コントローラの外枠の準備.
	s3d::Polygon Ctrlr;

	//L, Rボタンの準備.
	RectF Button_L;	//Lボタン.
	RectF Button_R;	//Rボタン.

	//L, Rトリガーの準備.
	RectF Trigger_L;	//Lトリガー.
	RectF Trigger_R;	//Rトリガー.

	//コントローラ外枠の上辺のy座標.
	double Top_y;

public:
	//UDP通信の初期化.
	void init_UDP() {
		// Winsockの初期化（WSAStartupを呼び出してWinsockライブラリを使用可能にする）.
		WSADATA wsaData;
		if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
			throw Error(U"WSAStartup failed");// 初期化に失敗した場合は例外を投げる.
		}

		// UDPソケットの作成（AF_INET: IPv4, SOCK_DGRAM: UDP, プロトコル指定なし）.
		sock = socket(AF_INET, SOCK_DGRAM, 0);
		if (sock == INVALID_SOCKET) {
			WSACleanup();// ソケット作成に失敗した場合はWSAをクリーンアップ.
			throw Error(U"Socket creation failed");// エラーメッセージを出して終了.
		}

		// サーバーのアドレス情報を設定.
		serverAddr.sin_family = AF_INET;// IPv4を使用.
		//serverAddr.sin_addr.s_addr = inet_addr(ip);// 文字列のIPアドレスを数値に変換.
		if (InetPtonA(AF_INET, ip, &serverAddr.sin_addr) != 1) {
			WSACleanup();
			throw Error(U"Invalid IP address format");
		}
		serverAddr.sin_port = htons(PORT);// ポート番号をネットワークバイトオーダーに変換.

		BOOL yes = 1;//ソケットオプションの設定.

		//プログラム再起動時、前回の接続がTIME_WAIT状態であっても、同じポート番号を再利用できるようにする.
		setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));
		if (sock == INVALID_SOCKET) {
			throw Error(U"Failed to create socket");
		}
	}

	JoyStick(uint16 port) :PORT(port) {}

	void initialize(const double center_x, const double center_y, const double size)
	{
		init_UDP();	//UDP通信の初期化.

		//送信するバッファの末にNULL文字を追加.
		send_buf[2] = '\0';

		//コントローラを表示する中心座標.
		Center_x = center_x;
		Center_y = center_y;
		r_Hex = size;

		// 概形の座標や大きさなどの情報.
		Dist_Cir_Big = r_Hex * sqrt(3.0) / (1.0 + sqrt(3.0));	//大円の中心と六角形の中心との距離.
		r_Cir_Big = r_Hex * sqrt(3.0) / (2.0 * (1.0 + sqrt(3.0)));	//大円の半径.
		//double r_Tri = r_Hex * 3.0 / (2.0 * (1.0 + sqrt(3.0))) * 2 / sqrt(3.0);		//大正三角形の一辺の長さ.
		Dist_Cir_Small = r_Hex * (2.0 * sqrt(3.0) + 1.0 - 2.0 * sqrt(1.0 + sqrt(3.0))) / (2.0 * (1.0 + sqrt(3.0)));	//小円と中心との距離.
		r_Cir_Small = Dist_Cir_Small / sqrt(3.0);	//小円の半径.

		//ボタンの大きさや座標などの情報.
		r_Button = r_Cir_Big / (1.0 + sqrt(2.0));//A, B, Y, Xボタンの半径.
		Dist_Button = r_Button * sqrt(2.0);//A, B, Y, Xボタンの大円中心からの距離.

		//コントローラの外枠.
		Top_y = Center_y - r_Hex * sqrt(3.0) / (2.0 * (1.0 + sqrt(3.0)));
		Vec2 Corner_L = { Center_x - r_Hex * (1.0 + 2.0 * sqrt(3.0)) / (2.0 * (1.0 + sqrt(3.0))), Top_y };
		Vec2 Corner_R = { Center_x + r_Hex * (1.0 + 2.0 * sqrt(3.0)) / (2.0 * (1.0 + sqrt(3.0))), Top_y };
		Ctrlr = s3d::Polygon
		{
		Corner_L,
		Corner_R,
		Vec2{Center_x + r_Hex * cos(0_deg), Center_y + r_Hex * sin(0_deg)},
		Vec2{Center_x + r_Hex * cos(60_deg), Center_y + r_Hex * sin(60_deg)},
		Vec2{Center_x + r_Hex * cos(120_deg), Center_y + r_Hex * sin(120_deg)},
		Vec2{Center_x + r_Hex * cos(180_deg), Center_y + r_Hex * sin(180_deg)}
		};

		//大円の中心座標の計算と大円の生成.
		for (int i = 0; i < 4; i++) {
			Arr_Loc_Cir_Big << Vec2(Center_x + Dist_Cir_Big * Cos((i * 60.0) * Math::Pi / 180.0), Center_y + Dist_Cir_Big * Sin((i * 60.0) * Math::Pi / 180.0));
			Arr_Cir_Big << Circle{ Arr_Loc_Cir_Big[i], r_Cir_Big };
		}

		//Back, Startボタンの表示の準備.
		Cir_Small_L = Circle{ Center_x - Dist_Cir_Small , Center_y + r_Cir_Small , r_Cir_Small };
		Cir_Small_R = Circle{ Center_x + Dist_Cir_Small , Center_y + r_Cir_Small , r_Cir_Small };

		//A, B, Y, Xボタンの準備.
		for (int i = 0; i < 4; i++) {
			Arr_ABXY << Circle{ Arr_Loc_Cir_Big[0].x + Dist_Button * Cos((i * 90) * Math::Pi / 180), Arr_Loc_Cir_Big[0].y + Dist_Button * Sin((i * 90) * Math::Pi / 180), r_Button };
		}

		//十字キーの準備.
		CrossKey = Shape2D::Plus(r_Cir_Big - 10, 30, Arr_Loc_Cir_Big[3], 0.0).asPolygon();
		for (int i = 0; i < 8; i++) {
			Arr_Cir_POV << Circle{ Arr_Loc_Cir_Big[3].x + Dist_Button * Cos((i * 45) * Math::Pi / 180), Arr_Loc_Cir_Big[3].y + Dist_Button * Sin((i * 45) * Math::Pi / 180), r_Button / 2 };
		}

		//L, Rボタンの準備.
		Button_L = RectF{ Corner_L.x, Corner_L.y - r_Cir_Small, r_Cir_Small * 2, r_Cir_Small };
		Button_R = RectF{ Corner_R.x - r_Cir_Small * 2, Corner_R.y - r_Cir_Small,r_Cir_Small * 2, r_Cir_Small };

		//L, Rトリガーの準備.
		Trigger_L = RectF{ Corner_L.x + r_Cir_Small * 2, Corner_L.y - r_Cir_Small * 2, r_Cir_Small * 2, r_Cir_Small * 2 };
		Trigger_R = RectF{ Corner_R.x - r_Cir_Small * 4, Corner_L.y - r_Cir_Small * 2, r_Cir_Small * 2, r_Cir_Small * 2 };
	}

	//UDP通信の終了.
	void cleanup() {
		if (sock != INVALID_SOCKET) {
			closesocket(sock);
		}
		WSACleanup();
	}

	//コントローラの状態を読み取る関数.
	void  JoyStick_Update()
	{
		// プレイヤーインデックス0の XInput コントローラを取得.
		controller = XInput(0);

		// それぞれデフォルト値を設定.
		controller.setLeftTriggerDeadZone();
		controller.setRightTriggerDeadZone();
		controller.setLeftThumbDeadZone();
		controller.setRightThumbDeadZone();
	}

	//コントローラの概形を描写する関数.
	void JoyStick_draw()
	{
		//コントローラの状態を読み取る.
		JoyStick_Update();

		//外枠表示.
		Ctrlr.draw(Palette::Steelblue).drawFrame(4, Palette::Black);

		//大円とABXYボタンの表示.
		for (int i = 0; i < 4; i++) {
			Arr_Cir_Big[i].draw(Palette::Lightgray).drawFrame(2, Palette::Black);
		}

		//十字キー表示.
		CrossKey.draw(Palette::White);
		Vec2 direction{	//円のベクトル.
				controller.buttonRight.pressed() - controller.buttonLeft.pressed(),
				controller.buttonDown.pressed() - controller.buttonUp.pressed()
		};
		if (!direction.isZero())
		{
			Circle{ Arr_Loc_Cir_Big[3] + direction.withLength(Dist_Button), r_Cir_Small }.draw(Palette::Crimson);
		}

		//A, B, X, Yボタン表示.
		Arr_ABXY[0].draw(controller.buttonB.pressed() ? Palette::Crimson : Palette::Snow).drawFrame(3, Palette::Black);//A
		Arr_ABXY[1].draw(controller.buttonA.pressed() ? Palette::Crimson : Palette::Snow).drawFrame(3, Palette::Black);//B
		Arr_ABXY[2].draw(controller.buttonX.pressed() ? Palette::Crimson : Palette::Snow).drawFrame(3, Palette::Black);//Y
		Arr_ABXY[3].draw(controller.buttonY.pressed() ? Palette::Crimson : Palette::Snow).drawFrame(3, Palette::Black);//X

		//左スティックの描画.
		Circle{ Arr_Cir_Big[2].center + Dist_Button * Vec2{controller.leftThumbX, -controller.leftThumbY}, r_Cir_Small }.draw(controller.buttonLThumb.pressed() ? Palette::Crimson : Palette::White).drawFrame(3, Palette::Black);

		//右スティックの描画.
		Circle{ Arr_Cir_Big[1].center + Dist_Button * Vec2{controller.rightThumbX, -controller.rightThumbY}, r_Cir_Small }.draw(controller.buttonRThumb.pressed() ? Palette::Crimson : Palette::White).drawFrame(3, Palette::Black);

		//Back, Startボタンの描画.
		Cir_Small_L.draw(controller.buttonView.pressed() ? Palette::Crimson : Palette::Snow).drawFrame(3, Palette::Black);	//小さいほうの円左側の描画.
		Cir_Small_R.draw(controller.buttonMenu.pressed() ? Palette::Crimson : Palette::Snow).drawFrame(3, Palette::Black);	//小さいほうの円右側の描画.

		//L, Rボタン描画.
		Button_L.rounded(10, 10, 0, 0).draw(controller.buttonLB.pressed() ? Palette::Crimson : Palette::Snow).drawFrame(3, Palette::Black);	//Lボタン描画.
		Button_R.rounded(10, 10, 0, 0).draw(controller.buttonRB.pressed() ? Palette::Crimson : Palette::Snow).drawFrame(3, Palette::Black);	//Rボタン描画.

		//L, Rトリガー描画.
		Trigger_L.rounded(10, 10, 0, 0).draw(Palette::White).drawFrame(3, Palette::Black);
		Trigger_L.stretched((controller.leftTrigger - 1.0) * Trigger_L.h, 0, 0, 0).rounded(10, 10, 0, 0).draw(Palette::Crimson).drawFrame(3, Palette::Black);;
		Trigger_R.rounded(10, 10, 0, 0).draw(Palette::White).drawFrame(3, Palette::Black);
		Trigger_R.stretched((controller.rightTrigger - 1.0) * Trigger_R.h, 0, 0, 0).rounded(10, 10, 0, 0).draw(Palette::Crimson).drawFrame(3, Palette::Black);;
	}

	//コントローラの状態を送信する関数.
	void send_UDP()
	{
		//コントローラの状態を読み取る.
		JoyStick_Update();

		send_UDP_accumlatedTime += Scene::DeltaTime();

		////送信データの作成.
		if (send_UDP_interval <= send_UDP_accumlatedTime) 
		{
			//送信するバッファのデフォルトでの2文字目.
			send_buf[1] = 0;
		
			if (controller.buttonUp.pressed() && !controller.buttonLeft.pressed() && !controller.buttonRight.pressed()) {
				//十字キー上.
				send_buf[0] = 't';
			}
			else if (controller.buttonDown.pressed() && !controller.buttonLeft.pressed() && !controller.buttonRight.pressed()) {
				//十字キー下.
				send_buf[0] = 'u';
			}
			else if (controller.buttonLeft.pressed() && !controller.buttonUp.pressed() && !controller.buttonDown.pressed()) {
				//十字キー左.
				send_buf[0] = 'e';
			}
			else if (controller.buttonRight.pressed() && !controller.buttonUp.pressed() && !controller.buttonDown.pressed()) {
				//十字キー右.
				send_buf[0] = 'a';
			}
			else if (controller.buttonUp.pressed() && controller.buttonLeft.pressed()) {
				//十字キー左上.
				send_buf[0] = 'd';
			}
			else if (controller.buttonUp.pressed() && controller.buttonRight.pressed()) {
				//十字キー右上.
				send_buf[0] = 'b';
			}
			else if (controller.buttonDown.pressed() && controller.buttonLeft.pressed()) {
				//十字キー左下.
				send_buf[0] = 'f';
			}
			else if (controller.buttonDown.pressed() && controller.buttonRight.pressed()) {
				//十字キー右下.
				send_buf[0] = 'h';
			}
			else if (controller.buttonB.pressed()) {
				//Aボタン.
				send_buf[0] = 'l';
			}
			else if (controller.buttonA.pressed()) {
				//Bボタン.
				send_buf[0] = 'm';
			}
			else if (controller.buttonY.pressed()) {
				//Xボタン.
				send_buf[0] = 'n';
			}
			else if (controller.buttonX.pressed()) {
				//Yボタン.
				send_buf[0] = 'o';
			}
			else if (controller.buttonLB.pressed()) {
				//LBボタン.//ZLボタン.
				send_buf[0] = 's';
			}
			else if (controller.buttonRB.pressed()) {
				//RBボタン.//ZRボタン.
				send_buf[0] = 'q';
			}
			else if (controller.leftThumbD8() == 0) {
				//左スティック上.
				send_buf[0] = 'c';
				send_buf[1] = static_cast<__int8>(100 * Vec2{ controller.leftThumbX, -controller.leftThumbY }.length());

			}
			else if (controller.leftThumbD8(0.5) == 1) {
				//左スティック右上.
				send_buf[0] = 'b';
				send_buf[1] = static_cast<__int8>(100 * Vec2{ controller.leftThumbX, -controller.leftThumbY }.length());
			}
			else if (controller.leftThumbD8() == 2) {
				//左スティック右.
				send_buf[0] = 'a';
				send_buf[1] = static_cast<__int8>(100 * Vec2{ controller.leftThumbX, -controller.leftThumbY }.length());
			}
			else if (controller.leftThumbD8(0.5) == 3) {
				//左スティック右下.
				send_buf[0] = 'h';
				send_buf[1] = static_cast<__int8>(100 * Vec2{ controller.leftThumbX, -controller.leftThumbY }.length());
			}
			else if (controller.leftThumbD8() == 4) {
				//左スティック下.
				send_buf[0] = 'g';
				send_buf[1] = static_cast<__int8>(100 * Vec2{ controller.leftThumbX, -controller.leftThumbY }.length());
			}
			else if (controller.leftThumbD8(0.5) == 5) {
				//左スティック左下.
				send_buf[0] = 'f';
				send_buf[1] = static_cast<__int8>(100 * Vec2{ controller.leftThumbX, -controller.leftThumbY }.length());
			}
			else if (controller.leftThumbD8() == 6) {
				//左スティック左.
				send_buf[0] = 'e';
				send_buf[1] = static_cast<__int8>(100 * Vec2{ controller.leftThumbX, -controller.leftThumbY }.length());
			}
			else if (controller.leftThumbD8(0.5) == 7) {
				//左スティック左上.
				send_buf[0] = 'd';
				send_buf[1] = static_cast<__int8>(100 * Vec2{ controller.leftThumbX, -controller.leftThumbY }.length());
			}
			else {
				//何も押されていない.
				send_buf[0] = 'k';
			}

			//データの送信.
			//send_UDP(send_buf);
			if (sendto(sock, send_buf, sizeof(send_buf), 0, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
				Print << U"Send failed";
			}
			else {
				////送信データの表示.
				//font(Unicode::Widen(send_buf)).drawAt(80, Vec2{ Center_x , Top_y + 40 }, ColorF{ 0 });
			}
			send_UDP_accumlatedTime -= send_UDP_interval;//累積時間のリセット.
		}
		//送信データの表示.
		font(Unicode::Widen(send_buf)).drawAt(80, Vec2{ Center_x , Top_y + 40 }, ColorF{ 0 });
	}
};

void Main()
{
	Scene::SetBackground(ColorF(0.8, 0.9, 1.0));
	const Font font(30);

	JoyStick ctrlr(5000);		//自作コントローラクラスの宣言.
	ctrlr.initialize(C_x, C_y, SIZE);

	while (System::Update())
	{
		ctrlr.JoyStick_draw();	//コントローラの描画.
		ctrlr.send_UDP();	//コントローラの状態を送信.
	}

	ctrlr.cleanup();	//UDP通信の終了.
}