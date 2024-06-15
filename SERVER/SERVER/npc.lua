myid = 99999;
targetid = 99999;
move_count = 0;

function set_uid(x)
   myid = x;
end

function event_player_move(player)
   player_x = API_get_x(player);
   player_y = API_get_y(player);
   my_x = API_get_x(myid);
   my_y = API_get_y(myid);
   if (player_x == my_x) then
      if (player_y == my_y) then
         targetid = player;
         API_SendMessage(myid, targetid, "HELLO");
         direction = math.random(0, 3);
		 API_SetDirection(myid, direction);
      end
   end
end

function event_3_move()
    if (move_count < 3) then
        move_count = move_count + 1;
	else
		move_count = 0;
        API_SendMessage(myid, targetid, "BYE");
        API_SetDirection(myid, -1);
	end
end