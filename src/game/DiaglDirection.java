package game;

public enum DiaglDirection 
{
	DIAGDIR_NE( 0 ),      // Northeast, upper right on your monitor 
	DIAGDIR_SE(  1 ),
	DIAGDIR_SW(  2 ),
	DIAGDIR_NW(  3 ),
	DIAGDIR_END( 4 ),
	INVALID_DIAGDIR ( 0xFF );
	
	private int value; 
	private DiaglDirection(int value) 
	{ 
		this.value = value; 
	}
	
	public int getValue() {
		return value;
	}
	
	public static DiaglDirection [] values = values();
}