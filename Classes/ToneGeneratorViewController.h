//
//  ToneGeneratorViewController.h
//  ToneGenerator
//
//  Created by Matt Gallagher on 2010/10/20.
//  Copyright 2010 Matt Gallagher. All rights reserved.
//
//  Permission is given to use this source code file, free of charge, in any
//  project, commercial or otherwise, entirely at your risk, with the condition
//  that any redistribution (in part or whole) of source code must retain
//  this copyright and permission notice. Attribution in compiled projects is
//  appreciated but not required.
//

#import <UIKit/UIKit.h>

@interface ToneGeneratorViewController : UIViewController
{
	UIButton *playButton, *generateButton;
    UITextView *textView;
    UISwitch *repeatSwitch;
    UIView *txView, *rxView;
}

@property (nonatomic, retain) IBOutlet UIButton *playButton;
@property (nonatomic, retain) IBOutlet UIButton *generateButton;
@property (nonatomic, retain) IBOutlet UITextView *textView;
@property (nonatomic, retain) IBOutlet UISwitch *repeatSwitch;
@property (nonatomic, retain) IBOutlet UIView *txView;
@property (nonatomic, retain) IBOutlet UIView *rxView;

@end

